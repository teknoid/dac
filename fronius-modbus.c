#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <ansi-color-codes.h>

#include "fronius-config.h"
#include "fronius.h"
#include "tasmota.h"
#include "sunspec.h"
#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

#define WAIT_AKKU			10
#define WAIT_RAMP			3
#define WAIT_NEXT			1

#define SFF(x, y)			(y == 0 ? x : (x) * pow(10, y))
#define SFI(x, y)			(y == 0 ? x : (int)((x) * pow(10, y)))
#define SFUI(x, y)			(y == 0 ? x : (unsigned int)((x) * pow(10, y)))

// program of the day - choose by mosmix forecast data
static potd_t *potd = 0;

// counter history
static counter_t counter_history[COUNTER_HISTORY];
static int counter_history_ptr = 0;
static volatile counter_t *counter = &counter_history[0];

// power state with actual power flow and state calculations
static pstate_t pstate_history[PSTATE_HISTORY];
static int pstate_history_ptr = 0;
static volatile pstate_t *pstate = &pstate_history[0];

// hourly state
static gstate_t gstate_history[24];
static volatile gstate_t *gstate = &gstate_history[0];

// load history during 1 hour
static int load_history[60];

// SunSpec modbus devices
static sunspec_t *f10 = 0, *f7 = 0, *meter = 0;

static struct tm *lt, now_tm, *now = &now_tm;
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

#ifndef FRONIUS_MAIN
	if (power)
		tasmota_power(heater->id, heater->r, 1);
	else
		tasmota_power(heater->id, heater->r, 0);
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
		d->state = Active;
		d->power = -1; // force set to 0
		(d->set_function)(d, 0);
		if (d->adjustable && d->addr == 0)
			d->state = Disabled; // controlled via socket send, so we need an ip address
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

static void dump_gstate() {
	char line[sizeof(pstate_t) * 12 + 16], value[16];
	int highlight = now->tm_hour > 0 ? now->tm_hour - 1 : 23;

	strcpy(line, "FRONIUS gstate   idx     pv   pv10    pv7  ↑grid  ↓grid  today   tomo    sun    exp   load    soc   akku  dakku   duty    ttl   mosm   surv");
	xdebug(line);
	for (int y = 0; y < 24; y++) {
		strcpy(line, "FRONIUS gstate ");
		if (y == highlight)
			strcat(line, BOLD);
		snprintf(value, 16, "[%3d] ", y);
		strcat(line, value);
		int *vv = (int*) &gstate_history[y];
		for (int x = 0; x < sizeof(gstate_t) / sizeof(int); x++) {
			snprintf(value, 12, "%6d ", vv[x]);
			strcat(line, value);
		}
		if (y == highlight)
			strcat(line, RESET);
		xdebug(line);
	}
}

static void dump_pstate(int back) {
	char line[sizeof(pstate_t) * 8 + 16], value[16];

	strcpy(line, "FRONIUS pstate  idx    pv   Δpv   grid Δgrid  akku  ac10   ac7  load Δload xload dxlod  dc10  10.1  10.2   dc7   7.1   7.2  surp  grdy modst  tend  wait   soc");
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
	// calculate new counter pointer
	if (++counter_history_ptr == COUNTER_HISTORY)
		counter_history_ptr = 0;
	counter_t *counter_new = &counter_history[counter_history_ptr];

	// take over all values
	memcpy(counter_new, (void*) counter, sizeof(counter_t));
	counter = counter_new; // atomic update current counter pointer
	counter->timestamp = now_ts; // mark actual day
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

static void print_dstate() {
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
	xlogl_int(line, 0, 0, "Sun", gstate->sun);
	xlogl_int(line, 0, 0, "Today", gstate->today);
	xlogl_int(line, 0, 0, "Tomo", gstate->tomorrow);
	xlogl_int(line, 0, 0, "Exp", gstate->expected);
	xlogl_int(line, 0, 0, "Load", gstate->load);
	xlogl_float(line, 0, 0, "SoC", FLOAT10(gstate->soc));
	xlogl_int(line, 0, 0, "Akku", gstate->akku);
	xlogl_int(line, 0, 0, "Duty", gstate->duty);
	xlogl_float(line, 0, 0, "TTL", FLOAT60(gstate->ttl));
	xlogl_float(line, 0, 0, "Mosmix", FLOAT10(gstate->mosmix));
	xlogl_float(line, 1, gstate->survive < 10, "Survive", FLOAT10(gstate->survive));
	strcat(line, " potd:");
	strcat(line, potd ? potd->name : "NULL");
	xlogl_end(line, strlen(line), message);
}

static void print_pstate(const char *message) {
	char line[512], value[16]; // 256 is not enough due to color escape sequences!!!
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
	if (f10 && f10->inverter) {
		snprintf(value, 16, " F10:%d/%d", f10->inverter->St, f10->poll);
		strcat(line, value);
	}
	if (f7 && f7->inverter) {
		snprintf(value, 16, " F7:%d/%d", f7->inverter->St, f7->poll);
		strcat(line, value);
	}
	xlogl_bits16(line, "Flags", pstate->flags);
	xlogl_end(line, strlen(line), message);
}

static void update_f10(sunspec_t *ss) {
	pstate->ac10 = SFI(ss->inverter->W, ss->inverter->W_SF);
	pstate->dc10 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);
	pstate->soc = SFF(ss->storage->ChaState, ss->storage->ChaState_SF) * 10;

	switch (ss->inverter->St) {
	case I_STATUS_MPPT:
		pstate->pv10_1 = SFI(ss->mppt->DCW1, ss->mppt->DCW_SF);
		pstate->pv10_2 = SFI(ss->mppt->DCW2, ss->mppt->DCW_SF);
		uint32_t x = SWAP32(ss->mppt->DCWH1);
		uint32_t y = SWAP32(ss->mppt->DCWH2);
		counter->pv10 = SFUI(x + y, ss->mppt->DCWH_SF);
		ss->poll = POLL_TIME_ACTIVE;
		ss->active = 1;
		break;

	case I_STATUS_SLEEPING:
		// let the inverter sleep
		ss->poll = POLL_TIME_SLEEPING;
		ss->active = 0;
		break;

	default:
		xdebug("FRONIUS %s inverter state %d", ss->name, ss->inverter->St);
		ss->active = 0;
		ss->poll = POLL_TIME_FAULT;
	}

	// akku power is DC power minus PV - calculate here as we need it in delta()
	pstate->akku = pstate->dc10 - (pstate->pv10_1 + pstate->pv10_2);
}

static void update_f7(sunspec_t *ss) {
	pstate->ac7 = SFI(ss->inverter->W, ss->inverter->W_SF);
	pstate->dc7 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);

	switch (ss->inverter->St) {
	case I_STATUS_MPPT:
		pstate->pv7_1 = SFI(ss->mppt->DCW1, ss->mppt->DCW_SF);
		pstate->pv7_2 = SFI(ss->mppt->DCW2, ss->mppt->DCW_SF);
		uint32_t x = SWAP32(ss->mppt->DCWH1);
		uint32_t y = SWAP32(ss->mppt->DCWH2);
		counter->pv7 = SFUI(x + y, ss->mppt->DCWH_SF);
		ss->poll = POLL_TIME_ACTIVE;
		ss->active = 1;
		break;

	case I_STATUS_SLEEPING:
		// let the inverter sleep
		ss->poll = POLL_TIME_SLEEPING;
		ss->active = 0;
		break;

	default:
		xdebug("FRONIUS %s inverter state %d", ss->name, ss->inverter->St);
		ss->active = 0;
		ss->poll = POLL_TIME_FAULT;
	}
}

static void update_meter(sunspec_t *ss) {
	uint32_t x = SWAP32(ss->meter->TotWhExp);
	uint32_t y = SWAP32(ss->meter->TotWhImp);
	counter->produced = SFUI(x, ss->meter->TotWh_SF);
	counter->consumed = SFUI(y, ss->meter->TotWh_SF);
	pstate->grid = SFI(ss->meter->W, ss->meter->W_SF);
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

	// charging akku has priority
	if (gstate->soc < 100)
		return select_program(&CHARGE);

	// we will NOT survive - charging akku has priority
	if (gstate->survive < 10)
		return select_program(&MODEST);

	// survive and less than 1h sun
	if (gstate->sun < 3600)
		return select_program(&MODEST);

	// tomorrow more pv than today - charge akku tommorrow
	if (gstate->tomorrow > gstate->today)
		return select_program(&GREEDY);

	// enough pv available
	return select_program(&SUNNY);
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
	// jump to last entry
	device_t **d = devices;
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

	// ramp up only when state is stable or enough surplus power
	int ok = PSTATE_STABLE || pstate->surplus > 2000;
	if (!ok)
		return 0;

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
	// do we have active devices?
	if (!PSTATE_ACTIVE)
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

	// no standby check indicated or state is not stable
	if (!PSTATE_CHECK_STANDBY || !PSTATE_STABLE)
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

	// ignore response below NOISE
	if (abs(delta) < NOISE) {
		xdebug("FRONIUS ignoring expected response below NOISE %d from %s", delta, d->name);
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

	// ignore standby check when power was released
	if (delta > 0)
		return 0;

	// perform standby check when noresponse counter reaches threshold
	if (++d->noresponse >= STANDBY_NORESPONSE)
		return perform_standby(d);

	xdebug("FRONIUS no response from %s count %d/%d", d->name, d->noresponse, STANDBY_NORESPONSE);
	return 0;
}

static void calculate_mosmix(time_t now_ts) {
	// reload mosmix data
	if (mosmix_load(CHEMNITZ))
		return;

	// gstate of last hour to match till now produced vs. till now predicted
	gstate_t *h = (gstate_t*) (gstate != &gstate_history[0] ? gstate - 1 : &gstate_history[23]);

	// sod+eod - values from midnight to now and now till next midnight
	mosmix_t sod, eod;
	mosmix_sod_eod(now_ts, &sod, &eod);

	// recalculate mosmix factor when we have pv: till now produced vs. till now predicted
	float mosmix;
	if (h->pv && sod.Rad1h) {
		mosmix = (float) h->pv / (float) sod.Rad1h;
		gstate->mosmix = mosmix * 10; // store as x10 scaled
	} else
		mosmix = lround((float) gstate->mosmix / 10.0); // take over existing value

	// expected pv power till end of day
	gstate->expected = eod.Rad1h * mosmix;
	xdebug("FRONIUS mosmix pv=%d sod=%d eod=%d expected=%d mosmix=%.1f", h->pv, sod.Rad1h, eod.Rad1h, gstate->expected, mosmix);

	// mosmix total expected today and tomorrow
	mosmix_t m0, m1;
	mosmix_24h(now_ts, 0, &m0);
	mosmix_24h(now_ts, 1, &m1);
	gstate->sun = m0.SunD1;
	gstate->today = m0.Rad1h * mosmix;
	gstate->tomorrow = m1.Rad1h * mosmix;
	xdebug("FRONIUS mosmix Rad1h/SunD1 today %d/%d, tomorrow %d/%d, exp today %d exp tomorrow %d", m0.Rad1h, m0.SunD1, m1.Rad1h, m1.SunD1, gstate->today, gstate->tomorrow);

	// calculate survival factor from needed to survive next night vs. available (expected + akku)
	int rad1h_min = gstate->duty / mosmix; // minimum value when we can live from pv and don't need akku anymore
	int needed = gstate->duty * mosmix_survive(now_ts, rad1h_min); // discharge * hours
	int available = gstate->expected + gstate->akku;
	float survive = needed ? lround((float) available) / ((float) needed) : 0;
	gstate->survive = survive * 10; // store as x10 scaled
	xdebug("FRONIUS mosmix needed=%d available=%d (%d expected + %d akku) survive=%.1f", needed, available, gstate->expected, gstate->akku, survive);
}

static void calculate_gstate() {
	gstate->soc = pstate->soc;

	// calculate daily values - when we have actual values and values from yesterday
	counter_t *y = get_counter_history(-1);
	gstate->produced = !counter->produced || !y->produced ? 0 : counter->produced - y->produced;
	gstate->consumed = !counter->consumed || !y->consumed ? 0 : counter->consumed - y->consumed;
	gstate->pv10 = !counter->pv10 || !y->pv10 ? 0 : counter->pv10 - y->pv10;
	gstate->pv7 = !counter->pv7 || !y->pv7 ? 0 : counter->pv7 - y->pv7;
	gstate->pv = gstate->pv10 + gstate->pv7;

	// get previous gstate slot to calculate deltas
	gstate_t *h = (gstate_t*) (gstate != &gstate_history[0] ? gstate - 1 : &gstate_history[23]);
	// xdebug("gstate %d, h %d", (long) gstate, (long) h);

	// calculate akku energy and and delta (+)charge (-)discharge when soc between 10-90%
	gstate->akku = AKKU_CAPACITY * gstate->soc / 1000;
	int range_ok = gstate->soc > 100 && gstate->soc < 900 && h->soc > 100 && h->soc < 900;
	gstate->dakku = range_ok ? AKKU_CAPACITY_SOC(gstate->soc) - AKKU_CAPACITY_SOC(h->soc) : 0;

	// calculate akku duty charge from mean akku discharge
	int sum = 0, count = 0;
	for (int i = 0; i < 24; i++) {
		gstate_t *g = &gstate_history[i];
		if (g->dakku < 0) { // discharge
			xdebug("FRONIUS akku duty discharge at %02d:00: %d Wh", i, g->dakku);
			sum += g->dakku;
			count++;
		}
	}

	// calculated akku duty charge and time to live
	gstate->duty = count ? sum / count * -1 : 0;
	gstate->ttl = gstate->duty ? gstate->akku * 60 / gstate->duty : 0; // minutes

	// calculate average load of last hour
	// TODO nachts wahrscheinlich zu wenig pstate werte weil zu wenig bewegung um diese zeit ???
	xlog_array_int(load_history, 60, "FRONIUS load");
	gstate->load = average_non_zero(load_history, 60);
	ZERO(load_history);
	xdebug("FRONIUS last hour mean load  %d", gstate->load);
}

static void calculate_pstate() {
	// clear all flags
	pstate->flags = 0;

	// get 2x history back
	pstate_t *h1 = get_pstate_history(-1);
	pstate_t *h2 = get_pstate_history(-2);

	// total PV produced by both inverters
	pstate->pv = pstate->pv10_1 + pstate->pv10_2 + pstate->pv7_1 + pstate->pv7_2;
	pstate->dpv = pstate->pv - h1->pv;
	if (pstate->pv < NOISE)
		pstate->pv = 0;

	// calculate delta grid
	pstate->dgrid = pstate->grid - h1->grid;
	if (abs(pstate->dgrid) < NOISE)
		pstate->dgrid = 0;

	// calculate load manually and store to minutely history
	pstate->load = (pstate->ac10 + pstate->ac7 + pstate->grid) * -1;
	load_history[now->tm_min] = pstate->load;

	// calculate delta load
	pstate->dload = pstate->load - h1->load;
	if (abs(pstate->dload) < NOISE)
		pstate->dload = 0;

	// offline mode when 3x not enough PV production
	if (pstate->pv < PV_MIN && h1->pv < PV_MIN && h2->pv < PV_MIN) {
		int burnout_time = !SUMMER && (now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8);
		int burnout_possible = TEMP_IN < 20 && pstate->soc > 150 && gstate->survive > 10;
		if (burnout_time && burnout_possible && AKKU_BURNOUT)
			pstate->flags |= FLAG_BURNOUT; // akku burnout between 6 and 9 o'clock when possible
		else
			pstate->flags |= FLAG_OFFLINE; // offline
		pstate->surplus = pstate->greedy = pstate->modest = pstate->xload = pstate->dxload = pstate->tendence = pstate->pv7_1 = pstate->pv7_2 = pstate->dpv = 0;
		return;
	}

	// emergency shutdown when three times extreme akku discharge or grid download
	if (pstate->akku > EMERGENCY && h1->akku > EMERGENCY && h2->akku > EMERGENCY && pstate->grid > EMERGENCY && h1->grid > EMERGENCY && h2->grid > EMERGENCY) {
		pstate->flags |= FLAG_EMERGENCY;
		return;
	}

	// state is stable when we have three times no grid changes
	int deltas = pstate->dgrid + h1->dgrid + h2->dgrid;
	if (!deltas)
		pstate->flags |= FLAG_STABLE;

	// check if we have active devices / all devices in standby
	pstate->flags |= FLAG_ALL_STANDBY;
	for (device_t **dd = DEVICES; *dd != 0; dd++) {
		device_t *d = *dd;
		if (d->state != Standby)
			pstate->flags &= ~FLAG_ALL_STANDBY;
		if (d->power)
			pstate->flags |= FLAG_ACTIVE;
	}

	// distortion when delta pv is too big
	int dpv_sum = 0;
	for (int i = 0; i < PSTATE_HISTORY; i++)
		dpv_sum += abs(pstate_history[i].dpv);
	if (dpv_sum > 1000)
		pstate->flags |= FLAG_DISTORTION;
	// xdebug("FRONIUS distortion=%d sum=%d", PSTATE_DISTORTION, dpv_sum);

	// pv tendence
	if (pstate->dpv < -NOISE && h1->dpv < -NOISE && h2->dpv < -NOISE)
		pstate->tendence = -1; // pv is continuously falling
	else if (pstate->dpv > NOISE && h1->dpv > NOISE && h2->dpv > NOISE)
		pstate->tendence = 1; // pv is continuously raising
	else
		pstate->tendence = 0;

	// calculate expected load - use average load between 03 and 04 or default BASELOAD
	pstate->xload = gstate_history[4].load ? gstate_history[4].load * -1 : BASELOAD;
	for (device_t **d = DEVICES; *d != 0; d++)
		pstate->xload += (*d)->load;
	pstate->xload *= -1;

	// deviation of calculated load to actual load in %
	pstate->dxload = (pstate->xload - pstate->load) * 100 / pstate->xload;

	// indicate standby check when deviation between actual load and calculated load is three times above 33%
	if (pstate->dxload > 33 && h1->dxload > 33 && h2->dxload > 33)
		pstate->flags |= FLAG_CHECK_STANDBY;

	// surplus is akku charge + grid upload
	pstate->surplus = (pstate->grid + pstate->akku) * -1;

	// greedy power = akku + grid
	// pstate->greedy = (pstate->surplus + h1->surplus + h2->surplus) / 3;
	pstate->greedy = pstate->surplus;
	if (pstate->greedy > 0)
		pstate->greedy -= NOISE; // threshold for ramp up
	if (abs(pstate->greedy) < NOISE)
		pstate->greedy = 0;
	if (!PSTATE_ACTIVE && pstate->greedy < 0)
		pstate->greedy = 0; // no active devices - nothing to ramp down

	// modest power = only grid upload
	// pstate->modest = (pstate->grid + h1->grid + h2->grid) / -3;
	pstate->modest = pstate->grid * -1;
	if (pstate->modest > 0)
		pstate->modest -= NOISE; // threshold for ramp up
	if (abs(pstate->modest) < NOISE)
		pstate->modest = 0;
	if (!PSTATE_ACTIVE && pstate->modest < 0)
		pstate->modest = 0; // no active devices - nothing to ramp down
	if (pstate->greedy < pstate->modest)
		pstate->modest = pstate->greedy; // greedy cannot be smaller than modest

	pstate->flags |= FLAG_VALID;

	// check and clear flag if values not valid
	int sum = pstate->grid + pstate->akku + pstate->load + pstate->pv10_1 + pstate->pv10_2 + pstate->pv7_1 + pstate->pv7_2;
	if (abs(sum) > SUSPICIOUS) {
		xdebug("FRONIUS suspicious values detected: sum=%d", sum); // probably inverter power dissipations (?)
//		pstate->flags &= ~FLAG_VALID;
	}
	if (pstate->load > 0) {
		xdebug("FRONIUS positive load detected");
//		pstate->flags &= ~FLAG_VALID;
	}
	if (pstate->grid < -NOISE && pstate->akku > NOISE) {
		int waste = abs(pstate->grid) < pstate->akku ? abs(pstate->grid) : pstate->akku;
		xdebug("FRONIUS wasting %d akku -> grid power", waste);
		pstate->flags &= ~FLAG_VALID;
	}
	if (pstate->grid - h1->grid > 500) { // e.g. refrigerator starts !!!
		xdebug("FRONIUS grid spike detected %d: %d -> %d", pstate->grid - h1->grid, h1->grid, pstate->grid);
		pstate->flags &= ~FLAG_VALID;
	}
	if (!potd) {
		xlog("FRONIUS no potd selected!");
		pstate->flags &= ~FLAG_VALID;
	}
	if (!f10->active) {
		xlog("FRONIUS Fronius10 is not active!");
		pstate->flags &= ~FLAG_VALID;
	}
}

static int delta() {
	// 3x history values
	pstate_t *h1 = get_pstate_history(-1);
	pstate_t *h2 = get_pstate_history(-2);
	pstate_t *h3 = get_pstate_history(-3);

	// not stable or no soc history value (startup)
	int stable = (h1->dgrid + h2->dgrid + h3->dgrid) == 0;
	if (!stable || !h1->soc)
		return 1;

	// do we have pv?
	int pv = pstate->pv10_1 > PV_MIN || pstate->pv10_2 > PV_MIN || pstate->pv7_1 > PV_MIN || pstate->pv7_2 > PV_MIN;

	// trigger on grid download or akku discharge when we have pv
	if (pv && (pstate->akku > NOISE || pstate->grid > NOISE))
		return 1;

	// trigger only on grid upload
	if (pv && potd == &MODEST)
		return pstate->grid < -NOISE;

	// trigger on both grid upload or akku charge
	if (pv && potd == &GREEDY)
		return (pstate->grid < -NOISE) || (pstate->akku < -NOISE);

	// trigger on any ac power changes
	int deltas = 0;
	deltas |= abs(pstate->grid - h1->grid) > NOISE;
	deltas |= abs(pstate->ac10 - h1->ac10) > NOISE;
	deltas |= abs(pstate->ac7 - h1->ac7) > NOISE;
	return deltas;
}

static void emergency() {
	xlog("FRONIUS emergency shutdown at %.1f akku discharge", FLOAT10(pstate->akku));
	set_all_devices(0);
}

static void offline() {
	// xlog("FRONIUS offline soc=%.1f temp=%.1f", FLOAT10(pstate->soc), TEMP_IN);
}

// burn out akku between 7 and 9 o'clock if we can re-charge it completely by day
static void burnout() {
	xlog("FRONIUS burnout soc=%.1f temp=%.1f", FLOAT10(pstate->soc), TEMP_IN);
	// fronius_override_seconds("plug5", WAIT_OFFLINE);
	// fronius_override_seconds("plug6", WAIT_OFFLINE);
	// fronius_override_seconds("plug7", WAIT_OFFLINE); // makes no sense due to ventilate sleeping room
	// fronius_override_seconds("plug8", WAIT_OFFLINE);
}

static void monthly(time_t now_ts) {
	xlog("FRONIUS executing monthly tasks...");

//	// reset minimum/maximum voltages
//	ZEROP(minmax);
//	minmax->v1min = minmax->v2min = minmax->v3min = minmax->fmin = 3000;
//	minmax->v1max = minmax->v2max = minmax->v3max = minmax->fmax = 0;

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
}

static void hourly(time_t now_ts) {
	xlog("FRONIUS executing hourly tasks...");

	// resetting noresponse counters and standby states
	for (device_t **dd = DEVICES; *dd != 0; dd++) {
		device_t *d = *dd;
		d->noresponse = 0;
		if (d->state == Standby)
			d->state = Active;
	}

	// force all devices off when offline
	if (PSTATE_OFFLINE)
		set_all_devices(0);

	// recalculate global state of elapsed hour
	calculate_gstate();

	// dump full gstate history
	dump_gstate();

	// update pointer to current hour slot and take over old values
	gstate_t *gstate_new = &gstate_history[now->tm_hour];
	memcpy(gstate_new, (void*) gstate, sizeof(gstate_t));
	gstate = gstate_new; // atomic update current gstate pointer

	// print actual gstate
	print_gstate(NULL);

	// recalculate mosmix and choose program of the day
	calculate_mosmix(now_ts);
	choose_program();

	// update voltage minimum/maximum
//	minimum_maximum(now_ts);
//	print_minimum_maximum();
}

static void fronius() {
	int hour, day, mon;
	device_t *device = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// initialize hourly & daily & monthly
	time_t last_ts = time(NULL), now_ts = time(NULL);
	localtime(&now_ts);
	memcpy(now, lt, sizeof(*lt));
	mon = now->tm_mon;
	day = now->tm_wday;
	hour = now->tm_hour;

	// fake yesterday
	// daily(now_ts);

	// wait for sunspec threads to produce data
	sleep(3);

	// once upon start: calculate global state + discharge rate and choose program of the day
	gstate = &gstate_history[now->tm_hour];
	calculate_gstate();
	calculate_mosmix(now_ts);
	choose_program();

	// the FRONIUS main loop
	while (1) {

		// wait for regulation to take effect or for new values
		if (device) {
			if (device->dload < 0 && pstate->soc < 900)
				sleep(WAIT_AKKU); // slow ramp up while akku not yet full
			else
				sleep(WAIT_RAMP); // fast ramp down + ramp up when full
		} else
			sleep(WAIT_NEXT);

		// get actual calendar time - and make a copy as subsequent calls to localtime() will override them
		now_ts = time(NULL);
		localtime(&now_ts);
		memcpy(now, lt, sizeof(*lt));

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

		// monthly tasks
		if (mon != now->tm_mon) {
			mon = now->tm_mon;
			monthly(now_ts);
		}

		// no device and no pstate value changes -> nothing to do
		if (!device && !delta())
			continue;

		// calculate new state
		calculate_pstate();

		// print actual pstate
		print_pstate(NULL);

		// check emergency
		if (PSTATE_EMERGENCY)
			emergency();

		// check offline
		if (PSTATE_OFFLINE)
			offline();

		// check burnout
		if (PSTATE_BURNOUT)
			burnout();

		// prio1: check response from previous action
		if (device)
			device = response(device);

		if (PSTATE_VALID) {
			// prio2: perform standby check logic
			if (!device)
				device = standby();

			// prio3: check if higher priorized device can steal from lower priorized
			if (!device)
				device = steal();

			// prio4: ramp up/down
			if (!device)
				device = ramp();
		}

		// print pstate history and device state when we have active devices
		pstate->wait = now_ts - last_ts;
		last_ts = now_ts;
		if (PSTATE_ACTIVE) {
			dump_pstate(3);
			print_dstate();
		}

		// set history pointer to next slot
		bump_pstate();
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
	int grid, voltage, closest, target;
	int offset_start = 0, offset_end = 0;
	int measure[1000], raster[101];

	// create a sunspec handle and remove models not needed
	sunspec_t *ss = sunspec_init("Meter", "192.168.25.230", 200);
	ss->inverter = 0;
	ss->storage = 0;
	ss->mppt = 0;

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	// write IP and port into sockaddr structure
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	printf("starting calibration on %s (%s)\n", name, addr);
	snprintf(message, 16, "v:0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// get maximum power, calculate 1%
//	printf("waiting for heat up 100%%...\n");
//	snprintf(message, 16, "v:10000:0");
//	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
//	sleep(5);
//	sunspec_read(ss);
//	grid = SFI(ss->meter->W, ss->meter->W_SF);
//	int max_power = round100(grid - offset_start);
	// TODO cmdline parameter
	int max_power = 2000;
	int onepercent = max_power / 100;
	printf("starting measurement with maximum power %d watt 1%%=%d watt\n", max_power, onepercent);

	// TODO !!! anders rum weil immer rampup gemacht wird und kalt völlige andere kurve
	// siehe misc/boiler2.txt validate ganz unten: ab 15% plötzlich 300 Watt, vorher nix !!!

	// average offset power at start
	printf("calculating offset start");
	for (int i = 0; i < 10; i++) {
		sunspec_read(ss);
		grid = SFI(ss->meter->W, ss->meter->W_SF);
		printf(" %d", grid);
		offset_start += grid;
		sleep(1);
	}
	offset_start = lround((float) offset_start / 10.0);
	printf(" --> average %d\n", offset_start);
	sleep(5);

	// do a full drive over SSR characteristic load curve from cold to hot and capture power
	for (int i = 0; i < 1000; i++) {
		voltage = i * 10;
		snprintf(message, 16, "v:%d:%d", voltage, 0);
		sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
		int sleep = 200 < i && i < 800 ? 1000 : 100; // slower between 2 and 8 volt
		msleep(sleep);
		sunspec_read(ss);
		measure[i] = SFI(ss->meter->W, ss->meter->W_SF) - offset_start;
		printf("%5d %5d\n", voltage, measure[i]);
	}

	// switch off
	snprintf(message, 16, "v:0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// average offset power at end
	printf("calculating offset end");
	for (int i = 0; i < 10; i++) {
		sunspec_read(ss);
		grid = SFI(ss->meter->W, ss->meter->W_SF);
		printf(" %d", grid);
		offset_end += grid;
		sleep(1);
	}
	offset_end = lround((float) offset_end / 10.0);
	printf(" --> average %d\n", offset_end);
	sleep(1);

	// build raster table
	raster[0] = 0;
	raster[100] = 10000;
	for (int i = 1; i < 100; i++) {

		// calculate next target power for table index (percent)
		target = onepercent * i;

		// find closest power to target power
		int min_diff = max_power;
		for (int j = 0; j < 1000; j++) {
			int diff = abs(measure[j] - target);
			if (diff < min_diff) {
				min_diff = diff;
				closest = j;
			}
		}

		// find all closest voltages that match target power +/- 5 watt
		int sum = 0, count = 0;
		printf("closest voltages to target power %5d matching %5d within +/-5 watt: ", target, measure[closest]);
		for (int j = 0; j < 1000; j++)
			if (measure[closest] - 5 < measure[j] && measure[j] < measure[closest] + 5) {
				printf(" %d:%d", measure[j], j * 10);
				sum += j * 10;
				count++;
			}

		// average of all closest voltages
		raster[i] = count ? lround((float) sum / (float) count) : 0;
		printf(" --> %dW %dmV\n", target, raster[i]);
	}

	// validate - values in measure table should grow, not shrink
	for (int i = 1; i < 1000; i++)
		if (measure[i - 1] > measure[i]) {
			int v_x = i * 10;
			int m_x = measure[i - 1];
			int v_y = (i - 1) * 10;
			int m_y = measure[i];
			printf("!!! WARNING !!! measuring tainted with parasitic power at voltage %d:%d > %d:%d\n", v_x, m_x, v_y, m_y);
		}
	if (offset_start != offset_end)
		printf("!!! WARNING !!! measuring tainted with parasitic power between start %d and end %d \n", offset_start, offset_end);

	// dump table
	printf("phase angle voltage table 0..100%% in %d watt steps:\n\n", onepercent);
	printf("%d, ", raster[0]);
	for (int i = 1; i <= 100; i++) {
		printf("%d, ", raster[i]);
		if (i % 10 == 0)
			printf("\\\n   ");
	}

	// validate
	printf("waiting 60s for cool down\n");
	sleep(60);
	for (int i = 0; i <= 100; i++) {
		snprintf(message, 16, "v:%d:%d", raster[i], 0);
		sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
		sleep(1);
		sunspec_read(ss);
		grid = SFI(ss->meter->W, ss->meter->W_SF) - offset_end;
		int expected = onepercent * i;
		float error = grid ? 100.0 - expected * 100.0 / (float) grid : 0;
		printf("%3d%% %5dmV expected %4dW actual %4dW error %.2f\n", i, raster[i], expected, grid, error);
	}

	// switch off
	snprintf(message, 16, "v:0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));

	// cleanup
	close(sock);
	sunspec_stop(ss);
	return 0;
}

static int test() {
	float grid;

	// create a sunspec handle and remove models not needed
	sunspec_t *ss = sunspec_init("Meter", "192.168.25.230", 200);
	ss->inverter = 0;
	ss->storage = 0;
	ss->mppt = 0;

	while (1) {
		sunspec_read(ss);
		grid = SFF(ss->meter->W, ss->meter->W_SF);
		printf("%.1f\n", grid);
		msleep(600);
	}

	return 0;
}

static int init() {
	set_debug(1);

	init_all_devices();
	set_all_devices(0);

	ZEROP(pstate_history);
	ZEROP(gstate_history);
	ZEROP(counter_history);
	ZERO(load_history);

	load_blob(GSTATE_FILE, gstate_history, sizeof(gstate_history));
	load_blob(COUNTER_FILE, counter_history, sizeof(counter_history));

	meter = sunspec_init_poll("Meter", "192.168.25.230", 200, &update_meter);
	f10 = sunspec_init_poll("Fronius10", "192.168.25.230", 1, &update_f10);
	f7 = sunspec_init_poll("Fronius7", "192.168.25.231", 2, &update_f7);

	// initialize localtime's static structure
	time_t sec = 0;
	lt = localtime(&sec);

	return 0;
}

static void stop() {
	sunspec_stop(f7);
	sunspec_stop(f10);
	sunspec_stop(meter);

#ifndef FRONIUS_MAIN
	store_blob(GSTATE_FILE, gstate_history, sizeof(gstate_history));
	store_blob_offset(COUNTER_FILE, counter_history, sizeof(*counter), COUNTER_HISTORY, counter_history_ptr);
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
