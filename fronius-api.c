#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include "fronius-config.h"
#include "fronius.h"
#include "tasmota.h"
#include "frozen.h"
#include "mosmix.h"
#include "utils.h"
#include "curl.h"
#include "mcp.h"

#define URL_READABLE		"http://fronius/components/readable"
#define URL_METER			"http://fronius/solar_api/v1/GetMeterRealtimeData.cgi?Scope=Device&DeviceId=0"
#define URL_FLOW10			"http://fronius10/solar_api/v1/GetPowerFlowRealtimeData.fcgi"
#define URL_FLOW7			"http://fronius7/solar_api/v1/GetPowerFlowRealtimeData.fcgi"

#define WAIT_OFFLINE		900
#define WAIT_STANDBY		300
#define WAIT_STABLE			60
#define WAIT_INSTABLE		20
#define WAIT_AKKU			10
#define WAIT_NEXT			5
#define WAIT_RAMP			3

// program of the day - choose by mosmix forecast data
static potd_t *potd = 0;

// global state with total counters and daily calculations
static gstate_t gstate_history[GSTATE_HISTORY], *gstate = &gstate_history[0];
static int gstate_history_ptr = 0;

// power state with actual power flow and state calculations
static pstate_t pstate_history[PSTATE_HISTORY], *pstate = &pstate_history[0];
static int pstate_history_ptr = 0;

// reading Inverter API: CURL handles, response memory, error counter
static CURL *curl10, *curl7, *curl_readable;
static response_t memory = { 0 };
static int errors;

// hourly collected akku discharge rates
static int discharge[24], discharge_soc, discharge_ts;

// meter storage for calibration
static meter_t m, *meter = &m;

static struct tm now_tm, *now = &now_tm;
static int sock = 0;

int set_heater(device_t *heater, int power) {
	// fix power value if out of range
	if (power < 0)
		power = 0;
	if (power > 1)
		power = 1;

	if (heater->override) {
		time_t t = time(NULL);
		if (t > heater->override) {
			xdebug("FRONIUS Override expired for %s", heater->name);
			heater->override = 0;
			power = 0;
		} else {
			xdebug("FRONIUS Override active for %lu seconds on %s", heater->override - t, heater->name);
			power = 100;
		}
	}

	// check if update is necessary
	if (heater->power && heater->power == power)
		return 0;

	// can we send a message
	if (heater->addr == NULL)
		return 0; // continue loop

	// char command[128];
#ifndef FRONIUS_MAIN
	if (power) {
		// xlog("FRONIUS switching %s ON", heater->name);
		// snprintf(command, 128, "curl --silent --output /dev/null http://%s/cm?cmnd=Power%%20On", heater->addr);
		// system(command);
		tasmota_power(heater->id, heater->r, 1);
	} else {
		// xlog("FRONIUS switching %s OFF", heater->name);
		// snprintf(command, 128, "curl --silent --output /dev/null http://%s/cm?cmnd=Power%%20Off", heater->addr);
		// system(command);
		tasmota_power(heater->id, heater->r, 0);
	}
#endif

	// update power values
	heater->power = power;
	heater->load = power ? heater->total : 0;
	heater->dload = power ? heater->total * -1 : heater->total;
	return 1; // loop done
}

// echo p:0:0 | socat - udp:boiler3:1975
int set_boiler(device_t *boiler, int power) {
	// fix power value if out of range
	if (power < 0)
		power = 0;
	if (power > 100)
		power = 100;

	if (boiler->override) {
		time_t t = time(NULL);
		if (t > boiler->override) {
			xdebug("FRONIUS Override expired for %s", boiler->name);
			boiler->override = 0;
			power = 0;
		} else {
			xdebug("FRONIUS Override active for %lu seconds on %s", boiler->override - t, boiler->name);
			power = 100;
		}
	}

	// no update necessary
	if (boiler->power && boiler->power == power)
		return 0; // continue loop

	// can we send a message
	if (boiler->addr == NULL)
		return 0; // continue loop

	// calculate step
	int step = power - boiler->power;

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock == 0)
		return xerr("Error creating socket");

	// write IP and port into sockaddr structure
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(boiler->addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	// send message to boiler
	char message[16];
	snprintf(message, 16, "p:%d:%d", power, 0);
	int ret = sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	if (ret < 0)
		return xerr("Sendto failed on %s %s", boiler->addr, strerror(ret));

	if (step < 0) {
		xdebug("FRONIUS ramp↓ %s step %d UDP %s", boiler->name, step, message);
	} else
		xdebug("FRONIUS ramp↑ %s step +%d UDP %s", boiler->name, step, message);

	// update power values
	boiler->power = power;
	boiler->load = boiler->total * boiler->power / 100;
	boiler->dload = boiler->total * step / -100;
	return 1; // loop done
}

static void set_all_devices(int power) {
	for (device_t **d = DEVICES; *d != 0; d++)
		((*d)->set_function)(*d, power);
}

// initialize all devices with start values
static void init_all_devices() {
	for (device_t **dd = DEVICES; *dd != 0; dd++) {
		device_t *d = *dd;
		d->addr = resolve_ip(d->name);
		if (d->addr == 0)
			d->state = Disabled;
		else {
			d->state = Active;
			d->power = -1; // force set to 0
			(d->set_function)(d, 0);
		}
	}
}

static gstate_t* get_gstate_history(int offset) {
	int i = gstate_history_ptr + offset;
	if (i < 0)
		i += GSTATE_HISTORY;
	if (i >= GSTATE_HISTORY)
		i -= GSTATE_HISTORY;
	return &gstate_history[i];
}

static pstate_t* get_pstate_history(int offset) {
	int i = pstate_history_ptr + offset;
	if (i < 0)
		i += PSTATE_HISTORY;
	if (i >= PSTATE_HISTORY)
		i -= PSTATE_HISTORY;
	return &pstate_history[i];
}

static void dump_gstate(int back) {
	char line[sizeof(pstate_t) * 12 + 16], value[12];

	strcpy(line,
			"FRONIUS gstate   idx         ts       pv10        pv7      ↑grid      ↓grid  pv10_24   pv7_24 ↑grid_24 ↓grid_24       pv      soc  survive expected    today tomorrow  dcharge      ttl   mosmix");
	xdebug(line);
	for (int y = 0; y < back; y++) {
		strcpy(line, "FRONIUS gstate ");
		snprintf(value, 12, "[%3d] ", y * -1);
		strcat(line, value);
		int *vv = (int*) get_gstate_history(y * -1);
		for (int x = 0; x < sizeof(gstate_t) / sizeof(int); x++) {
			snprintf(value, 12, (x < 5) ? "%10d " : "%8d ", vv[x]);
			strcat(line, value);
		}
		xdebug(line);
	}
}

static void dump_pstate(int back) {
	char line[sizeof(pstate_t) * 8 + 16], value[8];

	strcpy(line, "FRONIUS pstate  idx    pv   Δpv   grid  akku  surp  grdy modst   soc  load Δload xload dxlod cload  pv10   pv7  dist  tend stdby  wait");
	xdebug(line);
	for (int y = 0; y < back; y++) {
		strcpy(line, "FRONIUS pstate ");
		snprintf(value, 8, "[%2d] ", y * -1);
		strcat(line, value);
		int *vv = (int*) get_pstate_history(y * -1);
		for (int x = 0; x < sizeof(pstate_t) / sizeof(int); x++) {
			snprintf(value, 8, x == 2 ? "%6d " : "%5d ", vv[x]);
			strcat(line, value);
		}
		xdebug(line);
	}
}

static void bump_gstate() {
	dump_gstate(GSTATE_HISTORY);
	if (++gstate_history_ptr == GSTATE_HISTORY)
		gstate_history_ptr = 0;
	gstate = &gstate_history[gstate_history_ptr];
	ZERO(gstate);
}

static void bump_pstate() {
	dump_pstate(PSTATE_HISTORY);
	if (++pstate_history_ptr == PSTATE_HISTORY)
		pstate_history_ptr = 0;
	pstate = &pstate_history[pstate_history_ptr];
	ZERO(pstate);
}

static void print_gstate(const char *message) {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS gstate");
	xlogl_int_b(line, "PV", gstate->pv10_24 + gstate->pv7_24);
	xlogl_int_b(line, "PV10", gstate->pv10_24);
	xlogl_int_b(line, "PV7", gstate->pv7_24);
	xlogl_int(line, 1, 0, "↑Grid", gstate->grid_produced_24);
	xlogl_int(line, 1, 1, "↓Grid", gstate->grid_consumed_24);
	xlogl_int(line, 0, 0, "Expected", gstate->expected);
	xlogl_int(line, 0, 0, "Today", gstate->today);
	xlogl_int(line, 0, 0, "Tomorrow", gstate->tomorrow);
	xlogl_int(line, 0, 0, "Discharge", gstate->discharge);
	xlogl_int(line, 0, 0, "Survive", gstate->survive);
	xlogl_int(line, 1, AKKU_AVAILABLE < gstate->survive, "Available", AKKU_AVAILABLE);
	xlogl_int(line, 0, 0, "PV", gstate->pv);
	xlogl_float(line, "SoC", gstate->soc / 10.0);
	xlogl_float(line, "TTL", gstate->ttl / 60.0);
	xlogl_float(line, "Mosmix", gstate->mosmix / 10.0);
	xlogl_end(line, sizeof(line), message);
}

static void print_pstate(const char *message) {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS pstate");
	xlogl_int_b(line, "PV", pstate->pv);
	xlogl_int(line, 1, 1, "Grid", pstate->grid);
	xlogl_int(line, 1, 1, "Akku", pstate->akku);
	xlogl_int(line, 1, 0, "Surp", pstate->surplus);
	xlogl_int(line, 1, 0, "Greedy", pstate->greedy);
	xlogl_int(line, 1, 0, "Modest", pstate->modest);
	xlogl_int(line, 0, 0, "Load", pstate->load);
	xlogl_int(line, 0, 0, "ΔLoad", pstate->dload);
	xlogl_int(line, 0, 0, "PV10", pstate->pv10);
	xlogl_int(line, 0, 0, "PV7", pstate->pv7);
	xlogl_int(line, 0, 0, "Dist", pstate->distortion);
	xlogl_float(line, "SoC", pstate->soc / 10.0);
	xlogl_end(line, sizeof(line), message);
}

static void print_dstate(int wait) {
	char message[128];
	char value[5];

	strcpy(message, "FRONIUS device state ");
	for (device_t **d = DEVICES; *d != 0; d++) {
		snprintf(value, 5, "%d", (*d)->state);
		strcat(message, value);
	}

	strcat(message, "   power ");
	for (device_t **d = DEVICES; *d != 0; d++) {
		if ((*d)->adjustable)
			snprintf(value, 5, " %3d", (*d)->power);
		else
			snprintf(value, 5, "   %c", (*d)->power ? 'X' : '_');
		strcat(message, value);
	}

	strcat(message, "   noresponse ");
	for (device_t **d = DEVICES; *d != 0; d++) {
		snprintf(value, 5, "%d", (*d)->noresponse);
		strcat(message, value);
	}

	strcat(message, "   wait ");
	snprintf(value, 5, "%d", wait);
	strcat(message, value);

	xlog(message);
}

static int parse_fronius10(response_t *r) {
	float f_charge, f_akku, f_grid, f_load, f_pv;
	char *c;
	int ret;

	ret = json_scanf(r->buffer, r->size, "{ Body { Data { Site { P_Akku:%f, P_Grid:%f, P_Load:%f, P_PV:%f } } } }", &f_akku, &f_grid, &f_load, &f_pv);
	if (ret != 4)
		xlog("FRONIUS parse_fronius10() warning! parsing Body->Data->Site: expected 4 values but got %d", ret);

	pstate->akku = f_akku;
	pstate->grid = f_grid;
	pstate->load = f_load;
	pstate->pv10 = f_pv;

	// workaround parsing { "Inverters" : { "1" : { ... } } }
	ret = json_scanf(r->buffer, r->size, "{ Body { Data { Inverters:%Q } } }", &c);
	if (ret == 1 && c != NULL) {
		char *p = c;
		while (*p != '{')
			p++;
		p++;
		while (*p != '{')
			p++;

		ret = json_scanf(p, strlen(p) - 1, "{ SOC:%f }", &f_charge);
		if (ret == 1)
			pstate->soc = f_charge * 10.0; // store value as promille 0/00
		else {
			xlog("FRONIUS parse_fronius10() warning! parsing Body->Data->Inverters->SOC: no result");
			pstate->soc = 0;
		}

		free(c);
	} else
		xlog("FRONIUS parse_fronius10() warning! parsing Body->Data->Inverters: no result");

	return 0;
}

static int parse_fronius7(response_t *r) {
	float f_pv, f_total;
	int ret = json_scanf(r->buffer, r->size, "{ Body { Data { Site { P_PV:%f E_Total:%f } } } }", &f_pv, &f_total);
	if (ret != 2)
		return xerr("FRONIUS parse_fronius7() warning! parsing Body->Data->Site: expected 2 values but got %d", ret);

	pstate->pv7 = f_pv;
	gstate->pv7 = f_total;
	return 0;
}

static int parse_meter(response_t *r) {
	float f_grid, f_cons, f_prod;
	int ret = json_scanf(r->buffer, r->size, "{ Body { Data { PowerReal_P_Sum:%f, EnergyReal_WAC_Sum_Consumed:%f, EnergyReal_WAC_Sum_Produced:%f } } }", &f_grid, &f_cons, &f_prod);
	if (ret != 3)
		return xerr("FRONIUS parse_meter() warning! parsing Body->Data: expected 3 values but got %d", ret);

	ZERO(meter);
	meter->produced = f_prod;
	meter->consumed = f_cons;
	meter->p = f_grid;

	return 0;
}

static int parse_readable(response_t *r) {
	float f_pv, f_dc1, f_dc2, f_cons, f_prod, f_soc;
	int ret;
	char *p;

	// workaround for accessing inverter number as key: "262144" : {
	p = strstr(r->buffer, "\"262144\"") + 8 + 2;
	// xlog("FRONIUS %s", p);
	ret = json_scanf(p, r->size, "{ channels { PV_POWERACTIVE_SUM_F64:%f } }", &f_pv);
	if (ret != 1)
		return xerr("FRONIUS parse_readable() warning! parsing 262144: expected 1 values but got %d", ret);

	// workaround for accessing inverter number as key: "393216" : {
	p = strstr(r->buffer, "\"393216\"") + 8 + 2;
	// xlog("FRONIUS %s", p);
	ret = json_scanf(p, r->size, "{ channels { PV_ENERGYACTIVE_ACTIVE_SUM_01_U64:%f PV_ENERGYACTIVE_ACTIVE_SUM_02_U64:%f } }", &f_dc1, &f_dc2);
	if (ret != 2)
		return xerr("FRONIUS parse_readable() warning! parsing 393216: expected 2 values but got %d", ret);

	// workaround for accessing akku number as key: "16580608" : {
	p = strstr(r->buffer, "\"16580608\"") + 10 + 2;
	// xlog("FRONIUS %s", p);
	ret = json_scanf(p, r->size, "{ channels { BAT_VALUE_STATE_OF_CHARGE_RELATIVE_U16:%f } }", &f_soc);
	if (ret != 1)
		return xerr("FRONIUS parse_readable() warning! parsing 16580608: expected 1 values but got %d", ret);

	// workaround for accessing smartmeter number as key: "16252928" : {
	p = strstr(r->buffer, "\"16252928\"") + 10 + 2;
	// xlog("FRONIUS %s", p);
	ret = json_scanf(p, r->size, "{ channels { SMARTMETER_ENERGYACTIVE_CONSUMED_SUM_F64:%f SMARTMETER_ENERGYACTIVE_PRODUCED_SUM_F64:%f} }", &f_cons, &f_prod);
	if (ret != 2)
		return xerr("FRONIUS parse_readable() warning! parsing 16580608: expected 2 values but got %d", ret);

	gstate->pv10 = (f_dc1 / 3600) + (f_dc2 / 3600); // counters are in Ws!
	gstate->grid_produced = f_prod;
	gstate->grid_consumed = f_cons;
	gstate->soc = f_soc * 10.0; // store value as promille 0/00
	gstate->pv = f_pv;

	return 0;
}

static void select_program(const potd_t *p) {
	xlog("FRONIUS selecting %s program of the day", p->name);
	if (potd != p) {
		potd = (potd_t*) p;

		// potd has changed - reset all devices
		set_all_devices(0);

		// mark devices greedy/modest
		for (device_t **d = potd->greedy; *d != 0; d++)
			(*d)->greedy = 1;
		for (device_t **d = potd->modest; *d != 0; d++)
			(*d)->greedy = 0;
	}
}

// choose program of the day
static void choose_program() {
	int available = gstate->expected + AKKU_AVAILABLE;
	if (gstate->soc < 100 || available < gstate->survive)
		select_program(&EMERGENCY); // charge akku asap
	else {
		if (now->tm_hour > 12 && AKKU_AVAILABLE < gstate->survive)
			select_program(&MODEST); // afternoon - charge akku till we survive
		else {
			// we will survive
			if (gstate->tomorrow > BASELOAD * 24)
				select_program(&GREEDY); // charge akku tommorrow
			else
				select_program(&SUNNY); // enough available
		}
	}
}

// minimum available power for ramp up
static int rampup_min(device_t *d) {
	int min = d->adjustable ? d->total / 100 : d->total; // adjustable 1% of total, dumb total
	if (pstate->soc < 1000)
		min += min / 10; // 10% more while akku is charging to avoid excessive grid load
	if (pstate->distortion)
		min *= 2; // 100% more on distortion
	return min;
}

static int calculate_step(device_t *d, int power) {
	// power steps
	int step = power / (d->total / 100);
	xdebug("FRONIUS step1 %d", step);

	if (!step)
		return 0;

	// adjust step when ramp and tendence is same direction
	if (pstate->tendence) {
		if (power < 0 && pstate->tendence < 0)
			step--;
		if (power > 0 && pstate->tendence > 0)
			step++;
		xdebug("FRONIUS step2 %d", step);
	}

	// do smaller up steps / bigger down steps when we have distortion or akku is not yet full (give time to adjust)
	if (pstate->distortion || pstate->soc < 1000) {
		if (step > 1)
			step /= 2;
		if (step < -1)
			step *= 2;
		xdebug("FRONIUS step3 %d", step);
	}

	return step;
}

static int ramp_adjustable(device_t *d, int power) {
	// already full up
	if (d->power == 100 && power > 0)
		return 0;

	// already full down
	if (!d->power && power < 0)
		return 0;

	xdebug("FRONIUS ramp_adjustable() %s %d", d->name, power);
	int step = calculate_step(d, power);
	if (!step)
		return 0;

	return (d->set_function)(d, d->power + step);
}

static int ramp_dumb(device_t *d, int power) {
	// keep on as long as we have enough power and device is already on
	if (d->power && power > 0)
		return 0; // continue loop

	int min = rampup_min(d);
	xdebug("FRONIUS ramp_dumb() %s %d (min %d)", d->name, power, min);

	// switch on when enough power is available
	if (!d->power && power > min)
		return (d->set_function)(d, 1);

	// switch off
	if (d->power)
		return (d->set_function)(d, 0);

	return 0; // continue loop
}

static int ramp_device(device_t *d, int power) {
	if (d->state == Disabled || d->state == Standby)
		return 0; // continue loop

	if (d->adjustable)
		return ramp_adjustable(d, power);
	else
		return ramp_dumb(d, power);
}

static device_t* rampup(int power, device_t **devices) {
	for (device_t **d = devices; *d != 0; d++)
		if (ramp_device(*d, power))
			return *d;

	return 0; // next priority
}

static device_t* rampdown(int power, device_t **devices) {
	device_t **d = devices;

	// jump to last entry
	while (*d != 0)
		d++;

	// now go backward - this will give a reverse order
	while (d-- != devices)
		if (ramp_device(*d, power))
			return *d;

	return 0; // next priority
}

static device_t* ramp() {
	device_t *d;

	// if not yet done, do it now
	if (potd == 0)
		choose_program();

	// prio1: no extra power available: ramp down modest devices
	if (pstate->modest < 0) {
		d = rampdown(pstate->modest, potd->modest);
		if (d)
			return d;
	}

	// prio2: consuming grid power or discharging akku: ramp down greedy devices too
	if (pstate->greedy < 0) {
		d = rampdown(pstate->greedy, potd->greedy);
		if (d)
			return d;
	}

	// prio3: uploading grid power or charging akku: ramp up only greedy devices
	if (pstate->greedy > 0) {
		d = rampup(pstate->greedy, potd->greedy);
		if (d)
			return d;
	}

	// prio4: extra power available: ramp up modest devices too
	if (pstate->modest > 0) {
		d = rampup(pstate->modest, potd->modest);
		if (d)
			return d;
	}

	return 0;
}

static int steal_thief_victim(device_t *t, device_t *v) {
	if (t->state != Active || t->power || !v->load)
		return 0; // thief not active or already on or nothing to steal from victim

	int power = (t->greedy ? pstate->greedy : pstate->modest) + v->load;
	int min = rampup_min(t);
	if (power <= min)
		return 0; // not enough to steal

	xdebug("FRONIUS steal %d from %s %s and provide it to %s %s with a load of %d", v->load, GREEDY_MODEST(v), v->name, GREEDY_MODEST(t), t->name, t->total);
	ramp_device(v, v->load * -1);
	ramp_device(t, power);
	t->dload = 0; // force WAIT but no response expected as we put power from one to another device
	return 1;
}

static device_t* steal() {
	// greedy thief can steal from greedy victims behind
	for (device_t **t = potd->greedy; *t != 0; t++)
		for (device_t **v = t + 1; *v != 0; v++)
			if (steal_thief_victim(*t, *v))
				return *t;

	// greedy thief can steal from all modest victims
	for (device_t **t = potd->greedy; *t != 0; t++)
		for (device_t **v = potd->modest; *v != 0; v++)
			if (steal_thief_victim(*t, *v))
				return *t;

	// modest thief can steal from modest victims behind
	for (device_t **t = potd->modest; *t != 0; t++)
		for (device_t **v = t + 1; *v != 0; v++)
			if (steal_thief_victim(*t, *v))
				return *t;

	return 0;
}

static device_t* perform_standby(device_t *d) {
	d->state = Standby_Check;
	xdebug("FRONIUS starting standby check on %s", d->name);
	if (d->adjustable)
		// do a big ramp
		(d->set_function)(d, d->power + (d->power < 50 ? 25 : -25));
	else
		// toggle
		(d->set_function)(d, d->power ? 0 : 1);
	return d;
}

static int force_standby() {
	if (SUMMER)
		return 1; // summer mode -> off

	// force heating independently from temperature
	if ((now->tm_mon == 4 || now->tm_mon == 8) && now->tm_hour >= 16) // may/sept begin 16 o'clock
		return 0;
	else if ((now->tm_mon == 3 || now->tm_mon == 9) && now->tm_hour >= 14) // apr/oct begin 14 o'clock
		return 0;

	return TEMP_IN > 25; // too hot for heating
}

static device_t* standby() {
	// do we have powered devices?
	if (pstate->xload == BASELOAD)
		return 0;

	// put dumb devices into standby if summer or too hot
	if (force_standby(now)) {
		xdebug("FRONIUS month=%d out=%.1f in=%.1f --> forcing standby", now->tm_mon, TEMP_OUT, TEMP_IN);
		for (device_t **dd = DEVICES; *dd != 0; dd++) {
			device_t *d = *dd;
			if (!d->adjustable && d->state == Active) {
				(d->set_function)(d, 0);
				d->state = Standby;
			}
		}
	}

	// no standby check indicated
	if (!pstate->standby)
		return 0;

	// try first powered device with noresponse counter > 0
	for (device_t **d = DEVICES; *d != 0; d++)
		if ((*d)->state == Active && (*d)->power && (*d)->noresponse > 0)
			return perform_standby(*d);

	// try first powered device
	for (device_t **d = DEVICES; *d != 0; d++)
		if ((*d)->state == Active && (*d)->power)
			return perform_standby(*d);

	return 0;
}

static device_t* response(device_t *d) {
	// no delta power - no response to check
	int delta = d->dload;
	if (!delta)
		return 0;

	// reset
	d->dload = 0;

	// ignore response due to distortion or below NOISE
	if (pstate->distortion || abs(delta) < NOISE) {
		xdebug("FRONIUS ignoring expected response %d from %s (distortion=%d noise=%d)", delta, d->name, pstate->distortion, NOISE);
		d->state = Active;
		return 0;
	}

	// valid response is at least 1/3 of expected
	int response = pstate->dload != 0 && (delta > 0 ? (pstate->dload > delta / 3) : (pstate->dload < delta / 3));

	// response OK
	if (d->state == Active && response) {
		xdebug("FRONIUS response OK from %s, delta load expected %d actual %d", d->name, delta, pstate->dload);
		d->noresponse = 0;
		return 0;
	}

	// standby check was negative - we got a response
	if (d->state == Standby_Check && response) {
		xdebug("FRONIUS standby check negative for %s, delta load expected %d actual %d", d->name, delta, pstate->dload);
		d->noresponse = 0;
		d->state = Active;
		return d; // continue main loop
	}

	// standby check was positive -> set device into standby
	if (d->state == Standby_Check && !response) {
		xdebug("FRONIUS standby check positive for %s, delta load expected %d actual %d --> entering standby", d->name, delta, pstate->dload);
		(d->set_function)(d, 0);
		d->noresponse = 0;
		d->state = Standby;
		d->dload = 0; // no response expected
		return d; // continue main loop
	}

	// ignore standby check when switched off a dumb device
	if (!d->adjustable && delta > 0) {
		xdebug("FRONIUS skipping standby check for %s: switched off dumb device", d->name);
		return 0;
	}

	// perform standby check when noresponse counter reaches threshold
	if (++d->noresponse >= STANDBY_NORESPONSE)
		return perform_standby(d);

	xdebug("FRONIUS no response from %s", d->name);
	return 0;
}

static void calculate_gstate(time_t now_ts) {
	gstate->timestamp = now_ts;

	// yesterdays total counters and values
	gstate_t *yesterday = get_gstate_history(-1);

	// reload mosmix data
	if (mosmix_load(CHEMNITZ))
		return;

	// calculate daily values - when we have values from yesterday
	gstate->grid_produced_24 = yesterday->grid_produced ? gstate->grid_produced - yesterday->grid_produced : 0;
	gstate->grid_consumed_24 = yesterday->grid_consumed ? gstate->grid_consumed - yesterday->grid_consumed : 0;
	gstate->pv10_24 = yesterday->pv10 ? gstate->pv10 - yesterday->pv10 : 0;
	gstate->pv7_24 = yesterday->pv7 ? gstate->pv7 - yesterday->pv7 : 0;

	// sod+eod - values from midnight to now and now till next midnight
	mosmix_t sod, eod;
	mosmix_sod(&sod, now_ts);
	mosmix_eod(&eod, now_ts);

	// calculate actual mosmix factor: till now produced vs. till now predicted, available power and power needed to survive next night
	float mosmix = sod.Rad1h == 0 ? 1 : (float) (gstate->pv10_24 + gstate->pv7_24) / (float) sod.Rad1h;
	gstate->mosmix = 10 * mosmix; // store as x10 scaled
	int discharge = gstate->discharge == 0 ? BASELOAD : gstate->discharge; // nightly discharge rate
	float ttl = (float) AKKU_AVAILABLE / (float) discharge; // akku time to live in hours
	gstate->ttl = ttl * 60; // minutes
	gstate->survive = discharge * mosmix_survive(now_ts, discharge / mosmix); // BASELOAD * hours
	gstate->expected = eod.Rad1h * mosmix;
	int av = gstate->expected + AKKU_AVAILABLE;
	xdebug("FRONIUS mosmix sod=%d eod=%d available=%d (%d akku + %d pv)", sod.Rad1h, eod.Rad1h, av, AKKU_AVAILABLE, gstate->expected);

	// calculate mosmix total expected today and tomorrow
	mosmix_t m0, m1;
	mosmix_24h(&m0, now_ts, 0);
	mosmix_24h(&m1, now_ts, 1);
	gstate->today = m0.Rad1h * mosmix;
	gstate->tomorrow = m1.Rad1h * mosmix;
	xdebug("FRONIUS mosmix Rad1h/SunD1 today %d/%d, tomorrow %d/%d, today %d Wh, tomorrow %d Wh", m0.Rad1h, m0.SunD1, m1.Rad1h, m1.SunD1, gstate->today, gstate->tomorrow);

	// sum up all dumb loads
	int heating = 0;
	for (device_t **d = DEVICES; *d != 0; d++)
		if (!(*d)->adjustable)
			heating += (*d)->total;

	// find current mosmix slot and calculate hourly values
	// TODO do something with them
	mosmix_t *m = mosmix_current_slot(now_ts);
	if (m != 0) {
		int need_1h = AKKU_CAPACITY - AKKU_AVAILABLE;
		if (need_1h > 4500)
			need_1h = 4500; // max charge capacity per hour is 4,5kW
		need_1h += BASELOAD;
		need_1h += !SUMMER && now->tm_hour >= 9 && now->tm_hour < 15 ? heating : 0; // from 9 to 15 o'clock
		int exp_1h = m->Rad1h * mosmix;
		char *timestr = ctime(&m->ts);
		timestr[strcspn(timestr, "\n")] = 0; // remove any NEWLINE
		xdebug("FRONIUS mosmix current slot index=%d date=%s TTT=%.1f Rad1H=%d SunD1=%d, need1h/exp1h %d/%d Wh", m->idx, timestr, m->TTT, m->Rad1h, m->SunD1, need_1h, exp_1h);
	}
}

static int calculate_pstate() {
	// get 3x history back
	pstate_t *h1 = get_pstate_history(-1);
	pstate_t *h2 = get_pstate_history(-2);
	pstate_t *h3 = get_pstate_history(-3);

	// for validation
	int sum = pstate->grid + pstate->akku + pstate->load + pstate->pv10;

	// total pv from both inverters
	pstate->pv = pstate->pv10 + pstate->pv7;
	pstate->dpv = pstate->pv - h1->pv;

	// subtract PV produced by Fronius7
	pstate->load -= pstate->pv7;

	// calculate delta load
	pstate->dload = pstate->load - h1->load;
	if (abs(pstate->dload) < NOISE)
		pstate->dload = 0;

	// calculate load manually
	pstate->cload = (pstate->pv + pstate->akku + pstate->grid) * -1;

	// calculate expected load
	pstate->xload = BASELOAD;
	for (device_t **d = DEVICES; *d != 0; d++)
		pstate->xload += (*d)->load;
	pstate->xload *= -1;

	// deviation of calculated load to actual load in %
	pstate->dxload = (pstate->xload - pstate->load) * 100 / pstate->xload;

	// distortion when delta pv too big for last three times
	int dpv_sum = abs(h3->dpv) + abs(h2->dpv) + abs(h1->dpv) + abs(pstate->dpv);
	pstate->distortion = dpv_sum > 1000;

	// pv tendence
	if (h3->dpv < -NOISE && h2->dpv < -NOISE && h1->dpv < -NOISE && pstate->dpv < -NOISE)
		pstate->tendence = -1; // pv is continuously falling
	else if (h3->dpv > NOISE && h2->dpv > NOISE && h1->dpv > NOISE && pstate->dpv > NOISE)
		pstate->tendence = 1; // pv is continuously raising
	else
		pstate->tendence = 0;

	// indicate standby check when deviation between actual load and calculated load is three times over 33%
	pstate->standby = h3->dxload > 33 && h2->dxload > 33 && h1->dxload > 33 && pstate->dxload > 33 && !pstate->distortion;

	// surplus is akku charge + grid upload
	pstate->surplus = (pstate->grid + pstate->akku) * -1;

	// greedy power = akku + grid
	pstate->greedy = pstate->surplus - NOISE;
	if (abs(pstate->greedy) < NOISE)
		pstate->greedy = 0;

	// modest power = only grid upload without akku charge/discharge
	pstate->modest = pstate->surplus - abs(pstate->akku) - NOISE;
	if (abs(pstate->modest) < NOISE)
		pstate->modest = 0;

	// validate values
	if (abs(sum) > SUSPICIOUS) {
		xdebug("FRONIUS suspicious values detected: sum=%d", sum);
		return 0;
	}
	if (pstate->load > 0) {
		xdebug("FRONIUS positive load detected");
		return 0;
	}
	if (pstate->grid < -NOISE && pstate->akku > NOISE) {
		int waste = abs(pstate->grid) < pstate->akku ? abs(pstate->grid) : pstate->akku;
		xdebug("FRONIUS wasting %d akku -> grid power", waste);
		return 0;
	}
	if (abs(pstate->grid - h1->grid) > 1000) {
		xdebug("FRONIUS grid spike detected %d -> %d", h1->grid, pstate->grid);
		return 0;
	}

	return 1;
}

static int calculate_next_round(device_t *d, int valid) {
	if (d) {
		if (pstate->soc < 1000)
			return WAIT_AKKU; // wait for inverter to adjust charge power
		else
			return WAIT_RAMP;
	}

	// much faster next round on
	// - suspicious values detected
	// - pv tendence up/down
	// - distortion
	// - wasting akku->grid power
	// - big akku discharge or grid download
	// - actual load > calculated load --> other consumers active
	if (!valid || pstate->tendence || pstate->distortion || pstate->grid > 500 || pstate->akku > 500 || pstate->dxload < -5)
		return WAIT_NEXT;

	// all devices in standby?
	int all_standby = 1;
	for (device_t **d = DEVICES; *d != 0; d++)
		if ((*d)->state != Standby)
			all_standby = 0;
	if (all_standby)
		return WAIT_STANDBY;

	// state is stable when we had no power change now and within last 3 rounds
	int instable = pstate->dload;
	for (int i = 1; i <= 3; i++)
		instable += get_pstate_history(i * -1)->dload;
	if (instable)
		return WAIT_INSTABLE;

	return WAIT_STABLE;
}

// offline mode
static void offline() {
	set_all_devices(0);
	print_pstate("--> offline");
}

// burn out akku between 7 and 9 o'clock if we can re-charge it completely by day
static void burnout() {
	int burnout = pstate->soc > 100 && gstate->expected > gstate->survive && AKKU_BURNOUT && !SUMMER && TEMP_IN < 18;
	if (!burnout)
		return;

	fronius_override_seconds("plug5", WAIT_OFFLINE);
	fronius_override_seconds("plug6", WAIT_OFFLINE);
	// fronius_override_seconds("plug7", WAIT_OFFLINE); // makes no sense due to ventilate sleeping room
	fronius_override_seconds("plug8", WAIT_OFFLINE);

	char message[LINEBUF];
	snprintf(message, LINEBUF, "--> burnout soc=%.1f available=%d survive=%d temp=%.1f", pstate->soc / 10.0, gstate->expected, gstate->survive, TEMP_IN);
	print_pstate(message);
}

static void daily(time_t now_ts) {
	xlog("FRONIUS executing daily tasks...");

	// new day: bump global history and store to disk before writing new values into
	bump_gstate();
	store_blob_offset(GSTATE_FILE, gstate_history, sizeof(*gstate), GSTATE_HISTORY, gstate_history_ptr);
}

static void hourly(time_t now_ts) {
	xlog("FRONIUS executing hourly tasks...");

	// update gstate counter
	errors += curl_perform(curl_readable, &memory, &parse_readable);

	// calculate akku discharge rate for last hour when no PV and SoC between 90% and 10%
	if (gstate->pv == 0 && gstate->soc < 900 && gstate->soc > 100) {
		if (discharge_soc && discharge_ts) {
			// calculate
			int seconds = now_ts - discharge_ts;
			int start = AKKU_CAPA_SOC(discharge_soc);
			int end = AKKU_CAPA_SOC(gstate->soc);
			int lost = discharge[now->tm_hour] = start - end;
			xlog("FRONIUS calculated akku discharge rate for last hour: %d Wh, seconds=%d start=%d end=%d", lost, seconds, start, end);

			// dump hourly collected discharge rates
			char message[LINEBUF], value[6];
			strcpy(message, "FRONIUS discharge rate ");
			for (int i = 0; i < 24; i++) {
				snprintf(value, 6, "%4d ", discharge[i]);
				strcat(message, value);
			}
			xdebug(message);
		}
		// update for next calculation
		discharge_soc = gstate->soc;
		discharge_ts = now_ts;
	} else
		discharge_soc = discharge_ts = 0;

	// 12:00 calculate nightly mean discharge rate
	if (now->tm_hour == 12) {
		int sum = 0, count = 0;
		for (int i = 0; i < 24; i++)
			if (discharge[i]) { // sum up only non zero values
				sum += discharge[i];
				count++;
			}
		gstate->discharge = count == 0 ? 0 : sum / count;
		xlog("FRONIUS nightly mean akku discharge rate: %d Wh, sum=%d count=%d", gstate->discharge, sum, count);
		ZERO(discharge);
	}

	// recalculate global state and mosmix values
	calculate_gstate(now_ts);
	print_gstate(NULL);
	dump_gstate(2);

	xlog("FRONIUS resetting standby states");
	for (device_t **d = DEVICES; *d != 0; d++)
		if ((*d)->state == Standby)
			(*d)->state = Active;
}

static void fronius() {
	int hour, day, wait;
	device_t *device = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// initialize hourly & daily (do not run daily/hourly tasks at startup)
	time_t now_ts = time(NULL);
	struct tm *ltstatic = localtime(&now_ts);
	hour = ltstatic->tm_hour;
	day = ltstatic->tm_wday;

	errors = 0;
	wait = 1;

	// calculate gstate once upon start
	hourly(now_ts);

	// fake yesterday
	// daily(now_ts);

	// the FRONIUS main loop
	while (1) {

		sleep(1);
		if (wait--)
			continue;

		// get actual calendar time - and make a copy as subsequent calls to localtime() will override them
		now_ts = time(NULL);
		ltstatic = localtime(&now_ts);
		memcpy(now, ltstatic, sizeof(*ltstatic));

		// hourly tasks
		if (hour != now->tm_hour) {
			hour = now->tm_hour;
			hourly(now_ts);
		}

		// daily tasks
		if (day != now->tm_wday) {
			day = now->tm_wday;
			daily(now_ts);
		}

		// default
		wait = WAIT_NEXT;

		// check error counter
		if (errors > 10)
			set_all_devices(0);

		// make Fronius10 API call
		errors += curl_perform(curl10, &memory, &parse_fronius10);

		// not enough PV production
		if (pstate->pv10 < 100) {
			pstate->pv7 = 0;

			if (now->tm_hour == 7 || now->tm_hour == 8)
				burnout(); // akku burnout between 7 and 9 o'clock
			else
				offline(); // go into offline mode

			wait = WAIT_OFFLINE;
			continue;
		}

		// make Fronius7 API call
		errors += curl_perform(curl7, &memory, &parse_fronius7);

		// calculate actual state
		int valid = calculate_pstate();
		print_pstate(NULL);

		// values ok? then we can regulate
		if (valid) {

			// prio1: check response from previous action
			if (device)
				device = response(device);

			// prio2: perform standby check logic
			if (!device)
				device = standby();

			// prio3: ramp up/down
			if (!device)
				device = ramp();

			// prio4: check if higher priorized device can steal from lower priorized
			if (!device)
				device = steal();
		}

		// determine wait for next round
		wait = pstate->wait = calculate_next_round(device, valid);

		// set pstate history pointer to next slot
		bump_pstate();

		print_dstate(wait);
		errors = 0;
	}
}

// Kalibrierung über SmartMeter mit Laptop im Akku-Betrieb:
// - Nur Nachts
// - Akku aus
// - Külschränke aus
// - Heizung aus
// - Rechner aus
static int calibrate(char *name) {
	const char *addr = resolve_ip(name);
	char message[16];
	int voltage, closest, target, offset_start = 0, offset_end = 0;
	int measure[1000], raster[101];
	response_t memory = { 0 };

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	// write IP and port into sockaddr structure
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	CURL *curl = curl_init(URL_METER, &memory);
	if (curl == NULL)
		perror("Error initializing libcurl");

	printf("starting calibration on %s (%s)\n", name, addr);
	snprintf(message, 16, "v:0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// average offset power at start
	printf("calculating offset start");
	for (int i = 0; i < 10; i++) {
		curl_perform(curl, &memory, &parse_meter);
		offset_start += meter->p;
		printf(" %d", meter->p);
		sleep(1);
	}
	offset_start /= 10;
	printf(" --> average %d\n", offset_start);

	printf("waiting for heat up 100%%...\n");
	snprintf(message, 16, "v:10000:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// get maximum power
	curl_perform(curl, &memory, &parse_meter);
	int max_power = round100(meter->p - offset_start);

	int onepercent = max_power / 100;
	printf("starting measurement with maximum power %d watt 1%%=%d watt\n", max_power, onepercent);

	// do a full drive over SSR characteristic load curve from 10 down to 0 volt and capture power
	for (int i = 0; i < 1000; i++) {
		voltage = 10000 - (i * 10);

		snprintf(message, 16, "v:%d:%d", voltage, 0);
		sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));

		// give SSR time to set voltage and smart meter to measure
		if (2000 < voltage && voltage < 8000)
			usleep(1000 * 1000); // more time between 8 and 2 volts
		else
			usleep(1000 * 600);

		curl_perform(curl, &memory, &parse_meter);
		measure[i] = meter->p - offset_start;
		printf("%5d %5d\n", voltage, measure[i]);
	}

	// build raster table
	raster[0] = 10000;
	raster[100] = 0;
	for (int i = 1; i < 100; i++) {

		// calculate next target power -i%
		target = max_power - (onepercent * i);

		// find closest power to target power
		int min_diff = max_power;
		for (int j = 0; j < 1000; j++) {
			int diff = abs(measure[j] - target);
			if (diff < min_diff) {
				min_diff = diff;
				closest = j;
			}
		}

		// find all closest voltages that match target power
		int sum = 0, count = 0;
		printf("closest voltages to target power %5d matching %5d: ", target, measure[closest]);
		for (int j = 0; j < 1000; j++)
			if (measure[j] == measure[closest]) {
				printf("%5d", j);
				sum += 10000 - (j * 10);
				count++;
			}

		// average of all closest voltages
		raster[i] = sum / count;

		printf(" --> average %5d\n", raster[i]);
	}

	// average offset power at end
	printf("calculating offset end");
	for (int i = 0; i < 10; i++) {
		curl_perform(curl, &memory, &parse_meter);
		offset_end += meter->p;
		printf(" %d", meter->p);
		sleep(1);
	}
	offset_end /= 10;
	printf(" --> average %d\n", offset_end);

	// validate - values in measure table should shrink, not grow
	for (int i = 1; i < 1000; i++)
		if (measure[i - 1] < (measure[i] - 5)) { // with 5 watt tolerance
			int v_x = 10000 - (i * 10);
			int m_x = measure[i - 1];
			int v_y = 10000 - ((i - 1) * 10);
			int m_y = measure[i];
			printf("!!! WARNING !!! measuring tainted with parasitic power at voltage %d:%d < %d:%d\n", v_x, m_x, v_y, m_y);
		}
	if (offset_start != offset_end)
		printf("!!! WARNING !!! measuring tainted with parasitic power between start and end\n");

	// dump raster table in ascending order
	printf("phase angle voltage table 0..100%% in %d watt steps:\n\n", onepercent);
	printf("%d, ", raster[100]);
	for (int i = 99; i >= 0; i--) {
		printf("%d, ", raster[i]);
		if (i % 10 == 0)
			printf("\\\n");
	}

	// cleanup
	close(sock);
	curl_easy_cleanup(curl);
	return 0;
}

static int test() {
	response_t memory = { 0 };

	CURL *curl_readable = curl_init(URL_READABLE, &memory);
	if (curl_readable == NULL)
		return xerr("Error initializing libcurl");

	curl_perform(curl_readable, &memory, &parse_readable);
	return 0;

	device_t *d = &boiler1;

	d->power = -1;
	d->addr = resolve_ip(d->name);

	// 2 x blink
	set_boiler(d, 100);
	sleep(1);
	set_boiler(d, 0);
	sleep(1);
	set_boiler(d, 100);
	sleep(1);
	set_boiler(d, 0);
	sleep(1);

	// full ramp up 0..100
	for (int i = 0; i <= 100; i++) {
		set_boiler(d, i);
		usleep(200 * 1000);
	}
	set_boiler(d, 0);
	return 0;
}

static int init() {
	set_debug(1);

	init_all_devices();
	set_all_devices(0);
	ZERO(pstate_history);
	ZERO(gstate_history);
	ZERO(discharge);

	load_blob(GSTATE_FILE, gstate_history, sizeof(gstate_history));

	curl10 = curl_init(URL_FLOW10, &memory);
	if (curl10 == NULL)
		return xerr("Error initializing libcurl");

	curl7 = curl_init(URL_FLOW7, &memory);
	if (curl7 == NULL)
		return xerr("Error initializing libcurl");

	curl_readable = curl_init(URL_READABLE, &memory);
	if (curl_readable == NULL)
		return xerr("Error initializing libcurl");

	return 0;
}

static void stop() {
	store_blob_offset(GSTATE_FILE, gstate_history, sizeof(*gstate), GSTATE_HISTORY, gstate_history_ptr);

	if (sock != 0)
		close(sock);
}

int fronius_override_seconds(const char *name, int seconds) {
	for (device_t **d = DEVICES; *d != 0; d++) {
		if (!strcmp((*d)->name, name)) {
			xlog("FRONIUS Activating Override on %s", (*d)->name);
			(*d)->override = time(NULL) + seconds;
			(*d)->state = Active;
			((*d)->set_function)((*d), 100);
		}
	}
	return 0;
}

int fronius_override(const char *name) {
	return fronius_override_seconds(name, OVERRIDE);
}

int fronius_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	// no arguments - main loop
	if (argc == 1) {
		init();
		fronius();
		pause();
		stop();
		return 0;
	}

	init_all_devices();

	int c;
	while ((c = getopt(argc, argv, "c:o:t")) != -1) {
		switch (c) {
		case 'c':
			// execute as: stdbuf -i0 -o0 -e0 ./fronius -c boiler1 > boiler1.txt
			return calibrate(optarg);
		case 'o':
			return fronius_override(optarg);
		case 't':
			return test();
		}
	}

	return 0;
}

#ifdef FRONIUS_MAIN
int main(int argc, char **argv) {
	return fronius_main(argc, argv);
}
#else
MCP_REGISTER(fronius, 7, &init, &stop, &fronius);
#endif
