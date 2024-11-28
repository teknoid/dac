#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
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

#define JMC					" SMARTMETER_ENERGYACTIVE_CONSUMED_SUM_F64:%f "
#define JMP					" SMARTMETER_ENERGYACTIVE_PRODUCED_SUM_F64:%f "
#define JMV1				" SMARTMETER_VOLTAGE_MEAN_01_F64:%f "
#define JMV2				" SMARTMETER_VOLTAGE_MEAN_02_F64:%f "
#define JMV3				" SMARTMETER_VOLTAGE_MEAN_03_F64:%f "
#define JMF					" SMARTMETER_FREQUENCY_MEAN_F64:%f "

#define JBSOC				" BAT_VALUE_STATE_OF_CHARGE_RELATIVE_U16:%f "

#define JIE1				" PV_ENERGYACTIVE_ACTIVE_SUM_01_U64:%f "
#define JIE2				" PV_ENERGYACTIVE_ACTIVE_SUM_02_U64:%f "
#define JIP					" PV_POWERACTIVE_SUM_F64:%f "

#define JMMPP				" PowerReal_P_Sum:%f "
#define JMMC				" EnergyReal_WAC_Sum_Consumed:%f "
#define JMMP				" EnergyReal_WAC_Sum_Produced:%f "

// program of the day - choose by mosmix forecast data
static potd_t *potd = 0;

// counter history
static counter_t counter_history[COUNTER_HISTORY], *counter = &counter_history[0];
static int counter_history_ptr = 0;

// global state with total counters and daily calculations
static gstate_t gstate_history[GSTATE_HISTORY], *gstate = &gstate_history[0];
static int gstate_history_ptr = 0;

// power state with actual power flow and state calculations
static pstate_t pstate_history[PSTATE_HISTORY], *pstate = &pstate_history[0];
static int pstate_history_ptr = 0;

// hourly collected akku discharge rates
static int discharge[24], discharge_soc, discharge_ts;

// storage for holding minimum and maximum voltage values
static minmax_t mm, *minmax = &mm;

static struct tm now_tm, *now = &now_tm;
static int sock = 0;

// reading Inverter API: CURL handles, response memory, raw date, error counter
static CURL *curl10, *curl7, *curl_readable;
static response_t memory = { 0 };
static raw_t raw, *r = &raw;
static int errors;

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
// for i in `seq 1 10`; do let j=$i*10; echo p:$j:0 | socat - udp:boiler1:1975; sleep 1; done
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
#ifndef FRONIUS_MAIN
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
#endif

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

static counter_t* get_counter_history(int offset) {
	int i = counter_history_ptr + offset;
	if (i < 0)
		i += COUNTER_HISTORY;
	if (i >= COUNTER_HISTORY)
		i -= COUNTER_HISTORY;
	return &counter_history[i];
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

static void dump_counter(int back) {
	char line[sizeof(counter_t) * 12 + 16], value[16];

	strcpy(line, "FRONIUS counter   idx         ts       pv10        pv7      ↑grid      ↓grid");
	xdebug(line);
	for (int y = 0; y < back; y++) {
		strcpy(line, "FRONIUS counter ");
		snprintf(value, 16, "[%3d] ", y * -1);
		strcat(line, value);
		int *vv = (int*) get_counter_history(y * -1);
		for (int x = 0; x < sizeof(counter_t) / sizeof(int); x++) {
			snprintf(value, 12, "%10d ", vv[x]);
			strcat(line, value);
		}
		xdebug(line);
	}
}

static void dump_gstate(int back) {
	char line[sizeof(pstate_t) * 12 + 16], value[16];

	strcpy(line, "FRONIUS gstate   idx         ts     pv   pv10    pv7  ↑grid  ↓grid  today   tomo  bload    exp    soc    ttl   akku   noon   mosm   surv");
	xdebug(line);
	for (int y = 0; y < back; y++) {
		strcpy(line, "FRONIUS gstate ");
		snprintf(value, 16, "[%3d] ", y * -1);
		strcat(line, value);
		int *vv = (int*) get_gstate_history(y * -1);
		for (int x = 0; x < sizeof(gstate_t) / sizeof(int); x++) {
			snprintf(value, 12, x == 0 ? "%10d " : "%6d ", vv[x]);
			strcat(line, value);
		}
		xdebug(line);
	}
}

static void dump_pstate(int back) {
	char line[sizeof(pstate_t) * 8 + 16], value[16];

	strcpy(line, "FRONIUS pstate  idx    pv   Δpv   grid Δgrid  ac10   ac7  akku   soc  load Δload xload dxlod  dc10  10.1  10.2   dc7   7.1   7.2  surp  grdy modst  tend  wait");
	xdebug(line);
	for (int y = 0; y < back; y++) {
		strcpy(line, "FRONIUS pstate ");
		snprintf(value, 16, "[%2d] ", y * -1);
		strcat(line, value);
		int *vv = (int*) get_pstate_history(y * -1);
		for (int x = 0; x < sizeof(pstate_t) / sizeof(int) - 1; x++) {
			snprintf(value, 8, x == 2 ? "%6d " : "%5d ", vv[x]);
			strcat(line, value);
		}
		xdebug(line);
	}
}

static void bump_counter(time_t now_ts) {
	counter->timestamp = now_ts;

	// calculate new counter pointer
	if (++counter_history_ptr == COUNTER_HISTORY)
		counter_history_ptr = 0;
	counter_t *counter_new = &counter_history[counter_history_ptr];

	// take over all values
	memcpy(counter_new, (void*) counter, sizeof(counter_t));
	counter = counter_new; // atomic update current counter pointer
}

static void bump_gstate(time_t now_ts) {
	gstate->timestamp = now_ts;

	// calculate new gstate pointer
	if (++gstate_history_ptr == GSTATE_HISTORY)
		gstate_history_ptr = 0;
	gstate_t *gstate_new = &gstate_history[gstate_history_ptr];

	// take over all values
	memcpy(gstate_new, (void*) gstate, sizeof(gstate_t));
	gstate = gstate_new; // atomic update current gstate pointer
}

static void bump_pstate() {
	// calculate new pstate pointer
	if (++pstate_history_ptr == PSTATE_HISTORY)
		pstate_history_ptr = 0;
	pstate_t *pstate_new = &pstate_history[pstate_history_ptr];

	// take over all values
	memcpy(pstate_new, (void*) pstate, sizeof(pstate_t));
	pstate = pstate_new; // atomic update current pstate pointer
}

static void print_dstate(int wait) {
	char message[128], value[5];

	strcpy(message, "FRONIUS device power ");
	for (device_t **d = DEVICES; *d != 0; d++) {
		if ((*d)->adjustable)
			snprintf(value, 5, " %3d", (*d)->power);
		else
			snprintf(value, 5, "   %c", (*d)->power ? 'X' : '_');
		strcat(message, value);
	}

	strcat(message, "   state ");
	for (device_t **d = DEVICES; *d != 0; d++) {
		snprintf(value, 5, "%d", (*d)->state);
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

static void print_gstate(const char *message) {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS gstate");
	xlogl_int_b(line, "PV", gstate->pv);
	xlogl_int_b(line, "PV10", gstate->pv10);
	xlogl_int_b(line, "PV7", gstate->pv7);
	xlogl_int(line, 1, 0, "↑Grid", gstate->produced);
	xlogl_int(line, 1, 1, "↓Grid", gstate->consumed);
	xlogl_int(line, 0, 0, "Today", gstate->today);
	xlogl_int(line, 0, 0, "Tomorrow", gstate->tomorrow);
	xlogl_int(line, 0, 0, "Expected", gstate->expected);
	xlogl_int(line, 0, 0, "Baseload", gstate->baseload);
	xlogl_int(line, 0, 0, "Akku", gstate->akku);
	xlogl_float(line, 0, 0, "TTL", FLOAT60(gstate->ttl));
	xlogl_float(line, 0, 0, "SoC", FLOAT10(gstate->soc));
	xlogl_float(line, 0, 0, "Noon", FLOAT10(gstate->noon));
	xlogl_float(line, 0, 0, "Mosmix", FLOAT10(gstate->mosmix));
	xlogl_float(line, 1, gstate->survive < 10, "Survive", FLOAT10(gstate->survive));
	strcat(line, " potd:");
	strcat(line, potd ? potd->name : "NULL");
	xlogl_end(line, strlen(line), message);
}

static void print_pstate(const char *message) {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS pstate");
	xlogl_int_b(line, "PV", pstate->pv);
	xlogl_int_b(line, "PV10", pstate->pv10_1 + pstate->pv10_2);
	xlogl_int_b(line, "PV7", pstate->pv7_1 + pstate->pv7_2);
	xlogl_int(line, 1, 1, "Grid", pstate->grid);
	xlogl_int(line, 1, 1, "Akku", pstate->akku);
	xlogl_int(line, 1, 0, "Surp", pstate->surplus);
	xlogl_int(line, 1, 0, "Greedy", pstate->greedy);
	xlogl_int(line, 1, 0, "Modest", pstate->modest);
	xlogl_int(line, 0, 0, "Load", pstate->load);
	xlogl_int(line, 0, 0, "ΔLoad", pstate->dload);
	xlogl_float(line, 0, 0, "SoC", FLOAT10(pstate->soc));
	xlogl_bits(line, "Flags", pstate->flags);
	xlogl_end(line, strlen(line), message);
}

static void print_minimum_maximum() {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS");
	strcat(line, " Minima:");
	xlogl_float_b(line, "V1", FLOAT10(minmax->v1min));
	xlogl_float(line, 0, 0, "V2", FLOAT10(minmax->v12min));
	xlogl_float(line, 0, 0, "V3", FLOAT10(minmax->v13min));
	xlogl_float_b(line, "V2", FLOAT10(minmax->v2min));
	xlogl_float(line, 0, 0, "V1", FLOAT10(minmax->v21min));
	xlogl_float(line, 0, 0, "V3", FLOAT10(minmax->v23min));
	xlogl_float_b(line, "V3", FLOAT10(minmax->v3min));
	xlogl_float(line, 0, 0, "V1", FLOAT10(minmax->v31min));
	xlogl_float(line, 0, 0, "V2", FLOAT10(minmax->v32min));
	xlogl_float_b(line, "Hz", FLOAT10(minmax->fmin));
	strcat(line, "   Maxima:");
	xlogl_float_b(line, "V1", FLOAT10(minmax->v1max));
	xlogl_float(line, 0, 0, "V2", FLOAT10(minmax->v12max));
	xlogl_float(line, 0, 0, "V3", FLOAT10(minmax->v13max));
	xlogl_float_b(line, "V2", FLOAT10(minmax->v2max));
	xlogl_float(line, 0, 0, "V1", FLOAT10(minmax->v21max));
	xlogl_float(line, 0, 0, "V3", FLOAT10(minmax->v23max));
	xlogl_float_b(line, "V3", FLOAT10(minmax->v3max));
	xlogl_float(line, 0, 0, "V1", FLOAT10(minmax->v31max));
	xlogl_float(line, 0, 0, "V2", FLOAT10(minmax->v32max));
	xlogl_float_b(line, "Hz", FLOAT10(minmax->fmax));
	xlogl_end(line, strlen(line), NULL);
}

static void minimum_maximum_store(time_t now_ts, int *ts, int *v1, int *v2, int *v3) {
	*ts = now_ts;
	*v1 = r->v1 * 10.0;
	*v2 = r->v2 * 10.0;
	*v3 = r->v3 * 10.0;
}

static void minimum_maximum(time_t now_ts) {
	if (r->v1 * 10.0 < minmax->v1min)
		minimum_maximum_store(now_ts, &minmax->v1min_ts, &minmax->v1min, &minmax->v12min, &minmax->v13min);
	if (r->v2 * 10.0 < minmax->v2min)
		minimum_maximum_store(now_ts, &minmax->v2min_ts, &minmax->v21min, &minmax->v2min, &minmax->v23min);
	if (r->v3 * 10.0 < minmax->v3min)
		minimum_maximum_store(now_ts, &minmax->v3min_ts, &minmax->v31min, &minmax->v32min, &minmax->v3min);
	if (r->v1 * 10.0 > minmax->v1max)
		minimum_maximum_store(now_ts, &minmax->v1max_ts, &minmax->v1max, &minmax->v12max, &minmax->v13max);
	if (r->v2 * 10.0 > minmax->v2max)
		minimum_maximum_store(now_ts, &minmax->v2max_ts, &minmax->v21max, &minmax->v2max, &minmax->v23max);
	if (r->v3 * 10.0 > minmax->v3max)
		minimum_maximum_store(now_ts, &minmax->v3max_ts, &minmax->v31max, &minmax->v32max, &minmax->v3max);

	if (r->f * 10.0 < minmax->fmin) {
		minmax->fmin_ts = now_ts;
		minmax->fmin = r->f * 10.0;
	}
	if (r->f * 10.0 > minmax->fmax) {
		minmax->fmax_ts = now_ts;
		minmax->fmax = r->f * 10.0;
	}
}

static int parse_fronius10(response_t *resp) {
	char *c;
	int ret;

	ret = json_scanf(resp->buffer, resp->size, "{ Body { Data { Site { P_Akku:%f P_Grid:%f P_Load:%f P_PV:%f } } } }", &r->akku, &r->grid, &r->load, &r->pv10);
	if (ret != 4)
		xlog("FRONIUS parse_fronius10() warning! parsing Body->Data->Site: expected 4 values but got %d", ret);

	// workaround parsing { "Inverters" : { "1" : { ... } } }
	ret = json_scanf(resp->buffer, resp->size, "{ Body { Data { Inverters:%Q } } }", &c);
	if (ret == 1 && c != NULL) {
		char *p = c;
		while (*p != '{')
			p++;
		p++;
		while (*p != '{')
			p++;

		ret = json_scanf(p, strlen(p) - 1, "{ SOC:%f }", &r->soc);
		if (ret != 1)
			xlog("FRONIUS parse_fronius10() warning! parsing Body->Data->Inverters->SOC: no result");

		free(c);
	} else
		xlog("FRONIUS parse_fronius10() warning! parsing Body->Data->Inverters: no result");

	return 0;
}

static int parse_fronius7(response_t *resp) {
	int ret = json_scanf(resp->buffer, resp->size, "{ Body { Data { Site { P_PV:%f E_Total:%f } } } }", &r->pv7, &r->pv7_total);
	if (ret != 2)
		return xerr("FRONIUS parse_fronius7() warning! parsing Body->Data->Site: expected 2 values but got %d", ret);

	return 0;
}

static int parse_meter(response_t *resp) {
	int ret = json_scanf(resp->buffer, resp->size, "{ Body { Data { "JMMPP JMMC JMMP" } } }", &r->p, &r->consumed, &r->produced);
	if (ret != 3)
		return xerr("FRONIUS parse_meter() warning! parsing Body->Data: expected 3 values but got %d", ret);

	return 0;
}

static int parse_readable(response_t *resp) {
	int ret;
	char *p;

	// workaround for accessing inverter number as key: "262144" : {
	p = strstr(resp->buffer, "\"262144\"") + 8 + 2;
	ret = json_scanf(p, strlen(p), "{ channels { "JIP" } }", &r->pv10);
	if (ret != 1)
		return xerr("FRONIUS parse_readable() warning! parsing 262144: expected 1 values but got %d", ret);

	// workaround for accessing inverter number as key: "393216" : {
	p = strstr(resp->buffer, "\"393216\"") + 8 + 2;
	ret = json_scanf(p, strlen(p), "{ channels { "JIE1 JIE2" } }", &r->pv10_total1, &r->pv10_total2);
	if (ret != 2)
		return xerr("FRONIUS parse_readable() warning! parsing 393216: expected 2 values but got %d", ret);

	// workaround for accessing akku number as key: "16580608" : {
	p = strstr(resp->buffer, "\"16580608\"") + 10 + 2;
	ret = json_scanf(p, strlen(p), "{ channels { "JBSOC" } }", &r->soc);
	if (ret != 1)
		return xerr("FRONIUS parse_readable() warning! parsing 16580608: expected 1 values but got %d", ret);

	// workaround for accessing smartmeter number as key: "16252928" : {
	p = strstr(resp->buffer, "\"16252928\"") + 10 + 2;
	ret = json_scanf(p, strlen(p), "{ channels { "JMC JMP JMV1 JMV2 JMV3 JMF" } }", &r->consumed, &r->produced, &r->v1, &r->v2, &r->v3, &r->f);
	if (ret != 6)
		return xerr("FRONIUS parse_readable() warning! parsing 16252928: expected 6 values but got %d", ret);

	return 0;
}

static int select_program(const potd_t *p) {
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
	return 0;
}

// choose program of the day
static int choose_program() {

	// charge akku asap - no devices active
	if (gstate->soc < 100)
		return select_program(&CHARGE);

	// we will NOT survive - charging akku has priority
	if (gstate->survive < 10)
		return select_program(&MODEST);

	// enough pv available
	if (gstate->survive > 30)
		return select_program(&SUNNY);

	// afternoon is less sunny than forenoon - charge akku earlier
	if (gstate->noon < 10)
		return select_program(&MODEST);

	// tomorrow more pv than today - charge akku tommorrow
	if (gstate->tomorrow > gstate->today)
		return select_program(&GREEDY);

	// default
	return select_program(&MODEST);
}

// minimum available power for ramp up
static int rampup_min(device_t *d) {
	int min = d->adjustable ? d->total / 100 : d->total; // adjustable: 1% of total, dumb: total
	if (pstate->soc < 1000)
		min += min / 10; // 10% more while akku is charging to avoid excessive grid load
	if (PSTATE_DISTORTION)
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
	if (PSTATE_DISTORTION || pstate->soc < 1000) {
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
	if (!PSTATE_CHECK_STANDBY)
		return 0;

	// try first active powered device with noresponse counter > 0
	for (device_t **d = DEVICES; *d != 0; d++)
		if ((*d)->state == Active && (*d)->power && (*d)->noresponse > 0)
			return perform_standby(*d);

	// try first active powered adjustable device
	for (device_t **d = DEVICES; *d != 0; d++)
		if ((*d)->state == Active && (*d)->power && (*d)->adjustable)
			return perform_standby(*d);

	// try first active powered device
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
	if (PSTATE_DISTORTION || abs(delta) < NOISE) {
		xdebug("FRONIUS ignoring expected response %d from %s (distortion=%d noise=%d)", delta, d->name, PSTATE_DISTORTION ? 1 : 0, NOISE);
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

// TODO nicht akku discharge rate sondern über gstate history die Fronius10 lifetime counter berechnen
// collect akku discharge rate for last hour and calculate baseload
static void calculate_discharge_rate(time_t now_ts) {
	int last_discharge_soc = discharge_soc;
	int last_discharge_ts = discharge_ts;

	// calculate baseload from mean discharge rate
	if (now->tm_hour == 6) {
		gstate->baseload = average_non_zero(discharge, 24);
		xlog("FRONIUS calculated average nightly baseload %d Wh", gstate->baseload);
	}

	// clear it at high noon
	if (now->tm_hour == 12)
		ZERO(discharge);

	// update global values for next calculation
	discharge_soc = pstate->soc;
	discharge_ts = now_ts;

	// calculate only between 10% and 90%
	if (pstate->soc < 100 || pstate->soc > 900)
		return;

	int start = AKKU_CAPACITY_SOC(last_discharge_soc);
	int stop = AKKU_CAPACITY_SOC(pstate->soc);
	if (start < stop)
		return; // no discharge

	int seconds = now_ts - last_discharge_ts;
	int idx = now->tm_hour ? now->tm_hour - 1 : 23;
	int lost = discharge[idx] = start - stop;
	xlog("FRONIUS discharge rate last hour %d Wh, start=%d stop=%d seconds=%d", lost, start, stop, seconds);

	// dump hourly collected discharge rates
	char message[LINEBUF], value[6];
	strcpy(message, "FRONIUS discharge rate ");
	for (int i = 0; i < 24; i++) {
		snprintf(value, 6, "%4d ", discharge[i]);
		strcat(message, value);
	}
	xdebug(message);
}

static void calculate_gstate(time_t now_ts) {
	// take over raw values - counter
	counter->pv10 = (r->pv10_total1 / 3600) + (r->pv10_total2 / 3600); // counters are in Ws!
	counter->produced = r->produced;
	counter->consumed = r->consumed;
	if (r->pv7_total > 0.0)
		counter->pv7 = r->pv7_total; // don't take over zero as Fronius7 might be in sleep mode

	// check if baseload is available (e.g. not if akku is empty)
	if (!gstate->baseload) {
		int gridload = gstate->consumed - get_gstate_history(-1)->consumed;
		xdebug("FRONIUS no calculated baseload available, using last hour gridload %d as default", gridload);
		gstate->baseload = gridload;
	}
	if (gstate->baseload < NOISE) {
		xdebug("FRONIUS no reliable baseload available, using BASELOAD as default");
		gstate->baseload = BASELOAD;
	}

	// take over raw values - gstate
	gstate->timestamp = now_ts;
	gstate->soc = r->soc * 10.0; // store value as promille 0/00

	// yesterdays counters
	counter_t *y = get_counter_history(-1);

	// calculate daily values - when we have actual values and values from yesterday
	gstate->produced = !counter->produced || !y->produced ? 0 : counter->produced - y->produced;
	gstate->consumed = !counter->consumed || !y->consumed ? 0 : counter->consumed - y->consumed;
	gstate->pv10 = !counter->pv10 || !y->pv10 ? 0 : counter->pv10 - y->pv10;
	gstate->pv7 = !counter->pv7 || !y->pv7 ? 0 : counter->pv7 - y->pv7;
	gstate->pv = gstate->pv10 + gstate->pv7;

	// akku time to live calculated from baseload
	gstate->akku = AKKU_CAPACITY * (gstate->soc > 70 ? gstate->soc - 70 : 0) / 1000; // minus 7% minimum SoC
	float ttl = ((float) gstate->akku) / ((float) gstate->baseload); // hours
	gstate->ttl = ttl * 60.0; // minutes
}

static void calculate_mosmix(time_t now_ts) {
	// reload mosmix data
	if (mosmix_load(CHEMNITZ))
		return;

	// sod+eod - values from midnight to now and now till next midnight
	mosmix_t sod, eod;
	mosmix_sod(&sod, now_ts);
	mosmix_eod(&eod, now_ts);

	// actual mosmix factor: till now produced vs. till now predicted
	float mosmix = sod.Rad1h == 0 || gstate->pv == 0 ? 1 : ((float) gstate->pv) / ((float) sod.Rad1h);
	gstate->mosmix = mosmix * 10.0; // store as x10 scaled

	// expected pv power till end of day
	gstate->expected = eod.Rad1h * mosmix;

	// calculate survival factor from needed to survive next night vs. available (expected + akku)
	int rad1h_min = gstate->baseload / mosmix; // minimum value when we can live from pv and don't need akku anymore
	int needed = gstate->baseload * mosmix_survive(now_ts, rad1h_min); // discharge * hours
	int available = gstate->expected + gstate->akku;
	float survive = needed ? ((float) available) / ((float) needed) : 0;
	gstate->survive = survive * 10.0; // store as x10 scaled
	xdebug("FRONIUS mosmix needed=%d available=%d (%d expected + %d akku) survive=%.1f", needed, available, gstate->expected, gstate->akku, survive);

	// mosmix total expected today and tomorrow
	mosmix_t m0, m1;
	mosmix_24h(&m0, now_ts, 0);
	mosmix_24h(&m1, now_ts, 1);
	gstate->today = m0.Rad1h * mosmix;
	gstate->tomorrow = m1.Rad1h * mosmix;
	xdebug("FRONIUS mosmix sod=%d eod=%d Rad1h/SunD1 today %d/%d, tomorrow %d/%d", sod.Rad1h, eod.Rad1h, m0.Rad1h, m0.SunD1, m1.Rad1h, m1.SunD1);

	// mosmix total expected forenoon/afternoon
	mosmix_t bn, an;
	mosmix_noon(&bn, &an, now_ts);
	float noon = bn.SunD1 ? ((float) an.SunD1) / ((float) bn.SunD1) : 0;
	gstate->noon = noon * 10.0;
	xdebug("FRONIUS mosmix forenoon %d/%d, afternoon %d/%d, noon=%.1f", bn.Rad1h, bn.SunD1, an.Rad1h, an.SunD1, noon);
}

static void calculate_pstate1() {
	// take over raw values from Fronius10
	pstate->akku = r->akku;
	pstate->grid = r->grid;
	pstate->load = r->load;
	pstate->pv10_1 = r->pv10;
	pstate->soc = r->soc * 10.0;

	// get 2x history back
	pstate_t *h1 = get_pstate_history(-1);
	pstate_t *h2 = get_pstate_history(-2);

	// offline mode when 3x not enough PV production
	if (pstate->pv10_1 < PV_MIN && h1->pv10_1 < PV_MIN && h2->pv10_1 < PV_MIN) {
		int burnout_time = !SUMMER && (now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8);
		int burnout_possible = TEMP_IN < 20 && pstate->soc > 150 && gstate->survive > 10;
		if (burnout_time && burnout_possible && AKKU_BURNOUT)
			pstate->flags |= FLAG_BURNOUT; // akku burnout between 6 and 9 o'clock when possible
		else
			pstate->flags |= FLAG_OFFLINE; // offline
		return;
	}

	// emergency shutdown when 3x more than 10% capacity discharge
	if (pstate->akku > AKKU_CAPACITY / 10 && h1->akku > AKKU_CAPACITY / 10 && h2->akku > AKKU_CAPACITY / 10) {
		pstate->flags |= FLAG_EMERGENCY;
		return;
	}

	pstate->flags |= FLAG_VALID;
}

static void calculate_pstate2() {
	// take over raw values from Fronius7
	pstate->pv7_1 = r->pv7;

	// clear VALID flag
	pstate->flags &= ~FLAG_VALID;

	// get 2x history back
	pstate_t *h1 = get_pstate_history(-1);
	pstate_t *h2 = get_pstate_history(-2);

	// for validation
	int sum = pstate->grid + pstate->akku + pstate->load + pstate->pv10_1 + pstate->pv10_2;

	// total pv from both inverters
	pstate->pv = pstate->pv10_1 + pstate->pv10_2 + pstate->pv7_1 + pstate->pv7_2;
	pstate->dpv = pstate->pv - h1->pv;

	// subtract PV produced by Fronius7
	pstate->load -= (pstate->pv7_1 + pstate->pv7_2);

	// calculate delta load
	pstate->dload = pstate->load - h1->load;
	if (abs(pstate->dload) < NOISE)
		pstate->dload = 0;

	// calculate load manually
//	pstate->cload = (pstate->pv + pstate->akku + pstate->grid) * -1;

	// calculate expected load
	pstate->xload = BASELOAD;
	for (device_t **d = DEVICES; *d != 0; d++)
		pstate->xload += (*d)->load;
	pstate->xload *= -1;

	// deviation of calculated load to actual load in %
	pstate->dxload = (pstate->xload - pstate->load) * 100 / pstate->xload;

	// distortion when delta pv is 3x too big
	int dpv_sum = abs(h2->dpv) + abs(h1->dpv) + abs(pstate->dpv);
	if (dpv_sum > 1000)
		pstate->flags |= FLAG_DISTORTION;

	// pv tendence
	if (pstate->dpv < -NOISE && h1->dpv < -NOISE && h2->dpv < -NOISE)
		pstate->tendence = -1; // pv is continuously falling
	else if (pstate->dpv > NOISE && h1->dpv > NOISE && h2->dpv > NOISE)
		pstate->tendence = 1; // pv is continuously raising
	else
		pstate->tendence = 0;

	// indicate standby check when deviation between actual load and calculated load is three times over 33% and no distortion
	if (pstate->dxload > 33 && h1->dxload > 33 && h2->dxload > 33 && !PSTATE_DISTORTION)
		pstate->flags |= FLAG_CHECK_STANDBY;

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

	// all devices in standby?
	pstate->flags |= FLAG_ALL_STANDBY;
	for (device_t **d = DEVICES; *d != 0; d++)
		if ((*d)->state != Standby)
			pstate->flags &= ~FLAG_ALL_STANDBY;

	// state is stable when we have 3x no power change
	int deltas = pstate->dload + h1->dload + h2->dload;
	if (!deltas)
		pstate->flags |= FLAG_STABLE;

	// validate values
	if (abs(sum) > SUSPICIOUS) {
		xdebug("FRONIUS suspicious values detected: sum=%d", sum);
		return;
	}
	if (pstate->load > 0) {
		xdebug("FRONIUS positive load detected");
		return;
	}
	if (pstate->grid < -NOISE && pstate->akku > NOISE) {
		int waste = abs(pstate->grid) < pstate->akku ? abs(pstate->grid) : pstate->akku;
		xdebug("FRONIUS wasting %d akku -> grid power", waste);
		return;
	}
	if (abs(pstate->grid - h1->grid) > 500) { // e.g. refrigerator starts !!!
		xdebug("FRONIUS grid spike detected %d -> %d", h1->grid, pstate->grid);
		return;
	}
	if (!potd) {
		xlog("FRONIUS No potd selected!");
		return;
	}

	pstate->flags |= FLAG_VALID;
}

static int calculate_next_round(device_t *d) {
	if (d) {
		if (pstate->soc < 1000)
			return WAIT_AKKU; // wait for inverter to adjust charge power
		else
			return WAIT_RAMP;
	}

	if (PSTATE_OFFLINE || PSTATE_BURNOUT)
		return WAIT_OFFLINE;

	// much faster next round on
	// - suspicious values detected
	// - distortion
	// - pv tendence up/down
	// - wasting akku->grid power
	// - big akku discharge or grid download
	// - actual load > calculated load --> other consumers active
	if (!PSTATE_VALID || PSTATE_DISTORTION || pstate->tendence || pstate->grid > 500 || pstate->akku > 500 || pstate->dxload < -5)
		return WAIT_NEXT;

	if (PSTATE_ALL_STANDBY)
		return WAIT_STANDBY;

	if (PSTATE_STABLE)
		return WAIT_STABLE;

	return WAIT_INSTABLE;
}

static void emergency() {
	set_all_devices(0);
	xlog("FRONIUS emergency shutdown at %.1f akku discharge", FLOAT10(pstate->akku));
}

static void offline() {
	set_all_devices(0);
	xlog("FRONIUS offline pv=%d akku=%d grid=%d load=%d soc=%.1f", pstate->pv10_1, pstate->akku, pstate->grid, pstate->load, FLOAT10(pstate->soc));
}

// burn out akku between 7 and 9 o'clock if we can re-charge it completely by day
static void burnout() {
	fronius_override_seconds("plug5", WAIT_OFFLINE);
	fronius_override_seconds("plug6", WAIT_OFFLINE);
	// fronius_override_seconds("plug7", WAIT_OFFLINE); // makes no sense due to ventilate sleeping room
	fronius_override_seconds("plug8", WAIT_OFFLINE);
	xlog("FRONIUS burnout soc=%.1f temp=%.1f", FLOAT10(pstate->soc), TEMP_IN);
}

static void monthly(time_t now_ts) {
	xlog("FRONIUS executing monthly tasks...");

	// reset minimum/maximum voltages
	ZEROP(minmax);
	minmax->v1min = minmax->v2min = minmax->v3min = minmax->fmin = 3000;
	minmax->v1max = minmax->v2max = minmax->v3max = minmax->fmax = 0;

	// dump full counter history
	dump_counter(COUNTER_HISTORY);
}

static void daily(time_t now_ts) {
	xlog("FRONIUS executing daily tasks...");

	// bump counter history before writing new values into
	bump_counter(now_ts);

	// store to disk
#ifndef FRONIUS_MAIN
	store_blob_offset(COUNTER_FILE, counter_history, sizeof(*counter), COUNTER_HISTORY, counter_history_ptr);
//	store_blob(MINMAX_FILE, minmax, sizeof(minmax_t));
#endif

	// dump full gstate history
	dump_gstate(GSTATE_HISTORY);
}

static void hourly(time_t now_ts) {
	xlog("FRONIUS executing hourly tasks...");

	// bump gstate history before writing new values into
	bump_gstate(now_ts);

	// reset standby states
	xlog("FRONIUS resetting standby states");
	for (device_t **d = DEVICES; *d != 0; d++)
		if ((*d)->state == Standby)
			(*d)->state = Active;

	// update raw values
	errors += curl_perform(curl_readable, &memory, &parse_readable);

	// calculate global state
	calculate_gstate(now_ts);

	// calculate discharge rate and baseload
	calculate_discharge_rate(now_ts);

	// update voltage minimum/maximum
	minimum_maximum(now_ts);
	print_minimum_maximum();

	// recalculate global state with mosmix values
	calculate_mosmix(now_ts);

	// choose program of the day
	choose_program();
}

static void fronius() {
	int hour, day, mon, wait;
	device_t *device = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// initialize hourly & daily
	time_t now_ts = time(NULL);
	struct tm *ltstatic = localtime(&now_ts);
	mon = ltstatic->tm_mon;
	day = ltstatic->tm_wday;
	hour = ltstatic->tm_hour;

	errors = 0;
	wait = 1;

	// fake yesterday
	// daily(now_ts);

	// once upon start: calculate global state, init discharge and choose program of the day
	calculate_gstate(now_ts);
	calculate_discharge_rate(now_ts);
	choose_program();

	// the FRONIUS main loop
	while (1) {

		sleep(1);
		if (wait--)
			continue;

		// get actual calendar time - and make a copy as subsequent calls to localtime() will override them
		now_ts = time(NULL);
		ltstatic = localtime(&now_ts);
		memcpy(now, ltstatic, sizeof(*ltstatic));

		// monthly tasks
		if (mon != now->tm_mon) {
			mon = now->tm_mon;
			monthly(now_ts);
		}

		// daily tasks
		if (day != now->tm_wday) {
			day = now->tm_wday;
			daily(now_ts);
		}

		// hourly tasks
		if (hour != now->tm_hour) {
			hour = now->tm_hour;
			hourly(now_ts);
		}

		// check error counter
		if (errors > 10)
			set_all_devices(0);

		// dump and bump pstate history before writing new values into
		dump_pstate(5);
		bump_pstate();

		// make Fronius10 API call and calculate first pstate
		errors += curl_perform(curl10, &memory, &parse_fronius10);
		calculate_pstate1();

		// check burnout
		if (PSTATE_BURNOUT)
			burnout();

		// check offline
		if (PSTATE_OFFLINE)
			offline();

		// check emergency
		if (PSTATE_EMERGENCY)
			emergency();

		if (PSTATE_VALID) {
			// make Fronius7 API call and calculate second pstate
			errors += curl_perform(curl7, &memory, &parse_fronius7);
			calculate_pstate2();
		}

		// print actual gstate and pstate
		print_gstate(NULL);
		print_pstate(NULL);

		// values ok? then we can regulate
		if (PSTATE_VALID) {

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
		wait = pstate->wait = calculate_next_round(device);
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
	int voltage, closest, target;
	float offset_start = 0, offset_end = 0;
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
		offset_start += r->grid;
		printf(" %.1f", r->grid);
		sleep(1);
	}
	offset_start /= 10;
	printf(" --> average %.1f\n", offset_start);

	printf("waiting for heat up 100%%...\n");
	snprintf(message, 16, "v:10000:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// get maximum power
	curl_perform(curl, &memory, &parse_meter);
	int max_power = round100(r->grid - offset_start);

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
		measure[i] = r->grid - offset_start;
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
		offset_end += r->grid;
		printf(" %.1f", r->grid);
		sleep(1);
	}
	offset_end /= 10;
	printf(" --> average %.1f\n", offset_end);

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

	int x[] = { 264, 286, 264, 231, 275, 231, 220, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	int y = average_non_zero(x, ARRAY_SIZE(x));
	xlog("average_non_zero=%d sizeof=%d", y, ARRAY_SIZE(x));
	for (int i = 0; i < 24; i++)
		printf("%d ", x[i]);
	printf("\n");
	printf("sizeof %lu %lu\n", sizeof(x), sizeof(*x));
	ZERO(x);
	for (int i = 0; i < 24; i++)
		printf("%d ", x[i]);
	printf("\n");

	CURL *curl_readable = curl_init(URL_READABLE, &memory);
	if (curl_readable == NULL)
		return xerr("Error initializing libcurl");

	curl_perform(curl_readable, &memory, &parse_readable);
	return 0;

	device_t *d = &b1;

	d->power = -1;
	d->addr = resolve_ip(d->name);

	curl_perform(curl_readable, &memory, &parse_readable);
	printf("curl perform\n");

	return 0;
}

static int init() {
	set_debug(1);

	init_all_devices();
	set_all_devices(0);

	ZEROP(pstate_history);
	ZEROP(gstate_history);
	ZEROP(counter_history);
	ZEROP(r);
	ZERO(discharge);

	load_blob(COUNTER_FILE, counter_history, sizeof(counter_history));
	load_blob(GSTATE_FILE, gstate_history, sizeof(gstate_history));

	// load or default minimum/maximum
	if (load_blob(MINMAX_FILE, minmax, sizeof(minmax_t))) {
		minmax->v1min = minmax->v2min = minmax->v3min = minmax->fmin = 3000;
		minmax->v1max = minmax->v2max = minmax->v3max = minmax->fmax = 0;
	}

	curl10 = curl_init(URL_FLOW10, &memory);
	if (curl10 == NULL)
		return xerr("Error initializing libcurl");

	curl7 = curl_init(URL_FLOW7, &memory);
	if (curl7 == NULL)
		return xerr("Error initializing libcurl");

	curl_readable = curl_init(URL_READABLE, &memory);
	if (curl_readable == NULL)
		return xerr("Error initializing libcurl");

	// default global values
	if (!gstate->baseload)
		gstate->baseload = BASELOAD;

	return 0;
}

static void stop() {
#ifndef FRONIUS_MAIN
	store_blob_offset(COUNTER_FILE, counter_history, sizeof(*counter), COUNTER_HISTORY, counter_history_ptr);
	store_blob_offset(GSTATE_FILE, gstate_history, sizeof(*gstate), GSTATE_HISTORY, gstate_history_ptr);
	store_blob(MINMAX_FILE, minmax, sizeof(minmax_t));
#endif

	if (sock)
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
		printf("getopt %c\n", c);
		switch (c) {
		case 'c':
			// execute as: stdbuf -i0 -o0 -e0 ./fronius -c boiler1 > boiler1.txt
			return calibrate(optarg);
		case 'o':
			return fronius_override(optarg);
		case 't':
			test();
			printf("test\n");
			return 0;
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
