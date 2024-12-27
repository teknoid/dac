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

#define AKKU_CAPACITY_SOC(soc)	(SFI(f10->nameplate->WRtg, f10->nameplate->WRtg_SF) * soc / 1000)
#define EMERGENCY				(SFI(f10->nameplate->WRtg, f10->nameplate->WRtg_SF) / 10)
#define MIN_SOC					(SFI(f10->storage->MinRsvPct, f10->storage->MinRsvPct_SF) * 10)

#define WAIT_NEXT_RAMP_SLOW		10
#define WAIT_NEXT_RAMP_FAST		3
#define WAIT_RESPONSE			3
#define WAIT_NEXT				1

#define MOSMIX3X24				"FRONIUS mosmix Rad1h/SunD1/RSunD today %d/%d/%d tomorrow %d/%d/%d tomorrow+1 %d/%d/%d"

// program of the day - choosen by mosmix forecast data
static potd_t *potd = 0;

// counter history every hour over one day
static counter_t counter_hours[24];
static volatile counter_t *counter = 0;

// gstate history every hour over one day
static gstate_t gstate_hours[24];
static volatile gstate_t *gstate = 0;

// pstate history every second, minute, hour
static pstate_t pstate_seconds[60], pstate_minutes[60], pstate_hours[24];
static volatile pstate_t *pstate = 0;

// mosmix 24h forecasts today, tomorrow and tomorrow+1
static mosmix_file_t m0, m1, m2;

// SunSpec modbus devices
static sunspec_t *f10 = 0, *f7 = 0, *meter = 0;

static struct tm *lt, now_tm, *now = &now_tm;
static int sock = 0;

// forward declarations
static int select_program(const potd_t *p);
static void init_all_devices();

int fronius_boiler1() {
	return select_program(&BOILER1);
}

int fronius_boiler3() {
	return select_program(&BOILER3);
}

int fronius_override_seconds(const char *name, int seconds) {
#ifdef FRONIUS_MAIN
	init_all_devices();
#endif

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
	heater->xload = power ? heater->total * -1 : heater->total;
	heater->aload = pstate ? pstate->load : 0;
	heater->timer = WAIT_RESPONSE;
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
	boiler->xload = boiler->total * step / -100;
	boiler->aload = pstate ? pstate->load : 0;
	boiler->timer = WAIT_RESPONSE;
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

static int collect_pstate_load(int from, int hours) {
	int load = 0;
	char line[LINEBUF], value[25];
	strcpy(line, "FRONIUS mosmix load");
	for (int i = 0; i < hours; i++) {
		int hour = from + i;
		if (hour >= 24)
			hour -= 24;
		int hload = pstate_hours[hour].load * -1;
		load += hload;
		snprintf(value, 25, " %d:%d", hour, hload);
		strcat(line, value);
	}
	xdebug(line);
	return load;
}

static pstate_t* get_pstate_second(int offset) {
	int i = now->tm_sec + offset;
	while (i < 0)
		i += 60;
	while (i >= 60)
		i -= 60;
	return &pstate_seconds[i];
}

static pstate_t* get_pstate_minute(int offset) {
	int i = now->tm_min + offset;
	while (i < 0)
		i += 60;
	while (i >= 60)
		i -= 60;
	return &pstate_minutes[i];
}

static pstate_t* get_pstate_hour(int offset) {
	int i = now->tm_hour + offset;
	while (i < 0)
		i += 24;
	while (i >= 24)
		i -= 24;
	return &pstate_hours[i];
}

static gstate_t* get_gstate_hour(int offset) {
	int i = now->tm_hour + offset;
	while (i < 0)
		i += 24;
	while (i >= 24)
		i -= 24;
	return &gstate_hours[i];
}

static counter_t* get_counter_hour(int offset) {
	int i = now->tm_hour + offset;
	while (i < 0)
		i += 24;
	while (i >= 24)
		i -= 24;
	return &counter_hours[i];
}

static void print_gstate(const char *message) {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS");
	xlogl_int_b(line, "PV", gstate->pv);
	xlogl_int_b(line, "PV10", gstate->mppt1 + gstate->mppt2);
	xlogl_int_b(line, "PV7", gstate->mppt3 + gstate->mppt4);
	xlogl_int(line, 1, 0, "↑Grid", gstate->produced);
	xlogl_int(line, 1, 1, "↓Grid", gstate->consumed);
	xlogl_int(line, 0, 0, "Today", gstate->today);
	xlogl_int(line, 0, 0, "Tomo", gstate->tomorrow);
	xlogl_int(line, 0, 0, "Exp", gstate->expected);
	xlogl_float(line, 0, 0, "SoC", FLOAT10(gstate->soc));
	xlogl_int(line, 0, 0, "Akku", gstate->akku);
	xlogl_float(line, 0, 0, "TTL", FLOAT60(gstate->ttl));
	xlogl_float(line, 1, gstate->survive < 10, "Survive", FLOAT10(gstate->survive));
	strcat(line, " potd:");
	strcat(line, potd ? potd->name : "NULL");
	xlogl_end(line, strlen(line), message);
}

static void print_state(device_t *d) {
	char line[512], value[16]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS");

	for (device_t **d = DEVICES; *d != 0; d++) {
		if ((*d)->adjustable)
			snprintf(value, 5, " %3d", (*d)->power);
		else
			snprintf(value, 5, "   %c", (*d)->power ? 'X' : '_');
		strcat(line, value);
	}

	strcat(line, "   state ");
	for (device_t **d = DEVICES; *d != 0; d++) {
		snprintf(value, 5, "%d", (*d)->state);
		strcat(line, value);
	}

	strcat(line, "   nores ");
	for (device_t **d = DEVICES; *d != 0; d++) {
		snprintf(value, 5, "%d", (*d)->noresponse);
		strcat(line, value);
	}

	if (f10 && f10->inverter) {
		snprintf(value, 16, "   F10:%d", f10->inverter->St);
		strcat(line, value);
	}

	if (f7 && f7->inverter) {
		snprintf(value, 16, "   F7:%d", f7->inverter->St);
		strcat(line, value);
	}

	xlogl_bits16(line, "  Flags", pstate->flags);
	xlogl_int_b(line, "PV10", pstate->mppt1 + pstate->mppt2);
	xlogl_int_b(line, "PV7", pstate->mppt3 + pstate->mppt4);
	xlogl_int(line, 1, 1, "Grid", pstate->grid);
	xlogl_int(line, 1, 1, "Akku", pstate->akku);
	if (pstate->greedy)
		xlogl_int(line, 1, 0, "Greedy", pstate->greedy);
	if (pstate->modest)
		xlogl_int(line, 1, 0, "Modest", pstate->modest);
	xlogl_int(line, 0, 0, "Load", pstate->load);
	xlogl_float(line, 0, 0, "SoC", FLOAT10(pstate->soc));

	if (d)
		xlogl_int(line, 0, 0, d->name, d->timer);
	xlogl_end(line, strlen(line), 0);
}

static void update_f10(sunspec_t *ss) {
	if (!pstate || !counter)
		return;

	pstate->ac10 = SFI(ss->inverter->W, ss->inverter->W_SF);
	pstate->dc10 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);
	pstate->soc = SFF(ss->storage->ChaState, ss->storage->ChaState_SF) * 10;

	switch (ss->inverter->St) {
	case I_STATUS_MPPT:
		pstate->mppt1 = SFI(ss->mppt->DCW1, ss->mppt->DCW_SF);
		pstate->mppt2 = SFI(ss->mppt->DCW2, ss->mppt->DCW_SF);
		counter->mppt1 = SFUI(ss->mppt->DCWH1, ss->mppt->DCWH_SF);
		counter->mppt2 = SFUI(ss->mppt->DCWH2, ss->mppt->DCWH_SF);
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

static void update_f7(sunspec_t *ss) {
	if (!pstate || !counter)
		return;

	pstate->ac7 = SFI(ss->inverter->W, ss->inverter->W_SF);
	pstate->dc7 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);

	switch (ss->inverter->St) {
	case I_STATUS_FAULT:
		// TODO workaround meter - silently ignore
		// xdebug("FRONIUS %s inverter state %d", ss->name, ss->inverter->St);
		break;

	case I_STATUS_MPPT:
		pstate->mppt3 = SFI(ss->mppt->DCW1, ss->mppt->DCW_SF);
		pstate->mppt4 = SFI(ss->mppt->DCW2, ss->mppt->DCW_SF);
		counter->mppt3 = SFUI(ss->mppt->DCWH1, ss->mppt->DCWH_SF);
		counter->mppt4 = SFUI(ss->mppt->DCWH2, ss->mppt->DCWH_SF);
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
	if (!pstate || !counter)
		return;

	counter->produced = SFUI(ss->meter->TotWhExp, ss->meter->TotWh_SF);
	counter->consumed = SFUI(ss->meter->TotWhImp, ss->meter->TotWh_SF);
	pstate->grid = SFI(ss->meter->W, ss->meter->W_SF);
}

// define storage strategy
static void storage_strategy() {
	if (WINTER) {
		// winter mode

		// set minimum SoC to 10%
		sunspec_storage_minimum_soc(f10, 10);

		int limit = gstate->survive < 10 || gstate->tomorrow < 10000;
		if (limit)
			sunspec_storage_limit_discharge(f10, BASELOAD);
		else
			sunspec_storage_limit_reset(f10);	// no limits

	} else {

		// standard mode
		sunspec_storage_minimum_soc(f10, 5);
		sunspec_storage_limit_reset(f10);
	}
}

static int select_program(const potd_t *p) {
	if (potd == p)
		return 0;

	// potd has changed - reset all devices
	set_all_devices(0);

	xlog("FRONIUS selecting %s program of the day", p->name);
	potd = (potd_t*) p;

	// mark devices greedy/modest
	for (device_t **d = potd->greedy; *d != 0; d++)
		(*d)->greedy = 1;
	for (device_t **d = potd->modest; *d != 0; d++)
		(*d)->greedy = 0;

	return 0;
}

// choose program of the day
static int choose_program() {
	if (!gstate)
		return select_program(&MODEST);

	// we will NOT survive - charging akku has priority
	if (gstate->survive < 10)
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

	if (!step)
		return 0;

	// do smaller up steps / bigger down steps when we have distortion or akku is not yet full (give time to adjust)
	if (PSTATE_DISTORTION || pstate->soc < 1000) {
		if (step > 1)
			step /= 2;
		if (step < -1)
			step *= 2;
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

	// ramp up only when state is stable or enough power
	int ok = PSTATE_STABLE || pstate->greedy > 2000 || pstate->modest > 1000;
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
	t->xload = 0; // force WAIT but no response expected as we put power from one to another device
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
	if (force_standby()) {
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

	// ramp timer not yet expired -> continue main loop
	if (d->timer > 0) {
		d->timer--;
		return d;
	}

	// no expected delta load - no response to check
	if (!d->xload)
		return 0;

	int delta = pstate->load - d->aload;
	int expected = d->xload;
	d->xload = 0; // reset

	// ignore responses below NOISE
	if (abs(expected) < NOISE) {
		xdebug("FRONIUS ignoring expected response below NOISE %d from %s", expected, d->name);
		d->state = Active;
		return 0;
	}

	// valid response is at least 1/3 of expected
	int response = expected > 0 ? (delta > expected / 3) : (delta < expected / 3);

	// response OK
	if (d->state == Active && response) {
		xdebug("FRONIUS response OK from %s, delta load expected %d actual %d", d->name, expected, delta);
		d->noresponse = 0;
		d->timer = pstate->soc < 1000 ? WAIT_NEXT_RAMP_SLOW : WAIT_NEXT_RAMP_FAST; // akku needs ~10 seconds to ramp down charge power !!!
		return d;
	}

	// standby check was negative - we got a response
	if (d->state == Standby_Check && response) {
		xdebug("FRONIUS standby check negative for %s, delta load expected %d actual %d", d->name, expected, delta);
		d->noresponse = 0;
		d->state = Active;
		d->timer = pstate->soc < 1000 ? WAIT_NEXT_RAMP_SLOW : WAIT_NEXT_RAMP_FAST; // akku needs ~10 seconds to ramp down charge power !!!
		return d;
	}

	// standby check was positive -> set device into standby
	if (d->state == Standby_Check && !response) {
		xdebug("FRONIUS standby check positive for %s, delta load expected %d actual %d --> entering standby", d->name, expected, delta);
		(d->set_function)(d, 0);
		d->noresponse = 0;
		d->state = Standby;
		d->xload = 0; // no response from switch off expected
		return d; // recalculate in next round
	}

	// ignore standby check when power was released
	if (expected > 0)
		return 0;

	// perform standby check when noresponse counter reaches threshold
	if (++d->noresponse >= STANDBY_NORESPONSE)
		return perform_standby(d);

	xdebug("FRONIUS no response from %s count %d/%d", d->name, d->noresponse, STANDBY_NORESPONSE);
	return 0;
}

static void calculate_mosmix(time_t now_ts) {
	// update forecasts
	if (mosmix_load(now_ts, MARIENBERG))
		return;

	// update produced energy this hour and recalculate mosmix factors for each mppt
	mosmix_mppt(now->tm_hour, gstate->mppt1, gstate->mppt2, gstate->mppt3, gstate->mppt4);

	// dump
	mosmix_dump_today(now->tm_hour);
	mosmix_dump_tomorrow(now->tm_hour);

	// calculate expected
	int today, tomorrow, sod, eod;
	mosmix_expected(now->tm_hour, &today, &tomorrow, &sod, &eod);
	gstate->today = today;
	gstate->tomorrow = tomorrow;
	gstate->expected = eod;

	// calculate total daily values
	mosmix_24h(now_ts, 0, &m0);
	mosmix_24h(now_ts, 1, &m1);
	mosmix_24h(now_ts, 2, &m2);
	xdebug(MOSMIX3X24, m0.Rad1h, m0.SunD1, m0.RSunD, m1.Rad1h, m1.SunD1, m1.RSunD, m2.Rad1h, m2.SunD1, m2.RSunD);

	// calculate survival factor
	int hours, from, to;
	mosmix_survive(now->tm_hour, BASELOAD / 2, &hours, &from, &to);
	int available = gstate->expected + gstate->akku;
	int needed = collect_pstate_load(from, hours);
	float survive = needed ? (float) available / (float) needed : 0.0;
	gstate->survive = survive * 10; // store as x10 scaled
	xdebug("FRONIUS mosmix needed=%d available=%d (%d expected + %d akku) survive=%.2f", needed, available, gstate->expected, gstate->akku, survive);

	// actual vs. yesterdays expected ratio
	int actual = 0;
	for (int i = 0; i <= now->tm_hour; i++)
		actual += gstate_hours[i].pv;
	int yesterdays_tomorrow = gstate_hours[23].tomorrow;
	float error = yesterdays_tomorrow ? (float) actual / (float) yesterdays_tomorrow : 0;
	xdebug("FRONIUS mosmix yesterdays forecast for today %d, actual %d, error %.2f", yesterdays_tomorrow, actual, error);
}

static void calculate_gstate() {
	// take over SoC
	gstate->soc = pstate->soc;

	// get previous values to calculate deltas
	gstate_t *g = get_gstate_hour(-1);
	counter_t *c = get_counter_hour(-1);

	gstate->produced = counter->produced && c->produced ? counter->produced - c->produced : 0;
	gstate->consumed = counter->consumed && c->consumed ? counter->consumed - c->consumed : 0;
	gstate->mppt1 = counter->mppt1 && c->mppt1 ? counter->mppt1 - c->mppt1 : 0;
	gstate->mppt2 = counter->mppt2 && c->mppt2 ? counter->mppt2 - c->mppt2 : 0;
	gstate->mppt3 = counter->mppt3 && c->mppt3 ? counter->mppt3 - c->mppt3 : 0;
	gstate->mppt4 = counter->mppt4 && c->mppt4 ? counter->mppt4 - c->mppt4 : 0;
	gstate->pv = gstate->mppt1 + gstate->mppt2 + gstate->mppt3 + gstate->mppt4;

	// calculate akku energy and delta (+)charge (-)discharge when soc between 10-90% and estimate time to live when discharging
	gstate->akku = gstate->soc > MIN_SOC ? f10->nameplate->WHRtg * (gstate->soc - MIN_SOC) / 1000 : 0;
	int range_ok = gstate->soc > 100 && gstate->soc < 900 && g->soc > 100 && g->soc < 900;
	gstate->dakku = range_ok ? AKKU_CAPACITY_SOC(gstate->soc) - AKKU_CAPACITY_SOC(g->soc) : 0;
	if (gstate->dakku < 0)
		gstate->ttl = gstate->akku * 60 / gstate->dakku * -1; // in discharge phase - use current discharge rate (minutes)
	else if (gstate->soc > MIN_SOC)
		gstate->ttl = gstate->akku * 60 / BASELOAD; // not yet in discharge phase - use BASELOAD (minutes)
	else
		gstate->ttl = 0;
}

static void calculate_pstate() {
	// clear all flags
	pstate->flags = 0;

	// get history pstates
	gstate_t *g1 = get_gstate_hour(-1);
	pstate_t *m1 = get_pstate_minute(-1);
	pstate_t *m2 = get_pstate_minute(-2);
	pstate_t *s1 = get_pstate_second(-1);
	pstate_t *s2 = get_pstate_second(-2);

	// total PV produced by both inverters
	pstate->pv = pstate->mppt1 + pstate->mppt2 + pstate->mppt3 + pstate->mppt4;
	pstate->dpv = pstate->pv - s1->pv;
	pstate->sdpv = s1->sdpv + abs(pstate->dpv);

	// grid, delta grid and sum
	if (abs(pstate->grid) < NOISE)
		pstate->grid = 0; // shape
	pstate->dgrid = pstate->grid - s1->grid;
	if (abs(pstate->dgrid) < NOISE)
		pstate->dgrid = 0; // shape
	pstate->sdgrid = s1->sdgrid + abs(pstate->dgrid);

	// calculate load manually
	pstate->load = (pstate->ac10 + pstate->ac7 + pstate->grid) * -1;

	// calculate delta load + sum
	pstate->dload = pstate->load - s1->load;
	pstate->sdload = s1->sdload + abs(pstate->dload);

	// check if we have delta on any ac power flows
	if (abs(pstate->grid - s1->grid) > NOISE)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac10 - s1->ac10) > NOISE)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac7 - s1->ac7) > NOISE)
		pstate->flags |= FLAG_DELTA;

	// akku power is Fronius10 DC power minus PV
	pstate->akku = pstate->dc10 - (pstate->mppt1 + pstate->mppt2);
	if (abs(pstate->akku) < NOISE)
		pstate->akku = 0;

	// offline mode when 3x not enough PV production
	if (pstate->pv < NOISE && s1->pv < NOISE && s2->pv < NOISE) {
		int burnout_time = !SUMMER && (now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8);
		int burnout_possible = TEMP_IN < 20 && pstate->soc > 150 && g1->survive > 10;
		if (burnout_time && burnout_possible && AKKU_BURNOUT)
			pstate->flags |= FLAG_BURNOUT; // akku burnout between 6 and 9 o'clock when possible
		else
			pstate->flags |= FLAG_OFFLINE; // offline
		pstate->greedy = pstate->modest = pstate->xload = pstate->dxload = pstate->pv = pstate->dpv = pstate->mppt1 = pstate->mppt2 = pstate->mppt3 = pstate->mppt4 = 0;
		return;
	}

	// emergency shutdown when last minute extreme akku discharge or grid download
	if (m1->akku > EMERGENCY || m1->grid > EMERGENCY) {
		pstate->flags |= FLAG_EMERGENCY;
		return;
	}

	// state is stable when we have three times no grid changes
	if (!pstate->dgrid && !s1->dgrid && !s2->dgrid)
		pstate->flags |= FLAG_STABLE;

	// distortion when current sdpv is too big or aggregated last two sdpv's are too big
	if (pstate->sdpv > 20000 || m1->sdpv > 10000 || m2->sdpv > 10000) {
		pstate->flags |= FLAG_DISTORTION;
		xdebug("FRONIUS distortion=%d pstate->sdpv %d m1->sdpv %d m2->sdpv %d", PSTATE_DISTORTION, pstate->sdpv, m1->sdpv, m2->sdpv);
	}

	// device loop:
	// - expected load
	// - active devices
	// - all devices in standby
	pstate->flags |= FLAG_ALL_STANDBY;
	//	pstate->xload = pstate_hours[4].load ? pstate_hours[4].load * -1 : BASELOAD;
	pstate->xload = BASELOAD;
	for (device_t **dd = DEVICES; *dd != 0; dd++) {
		device_t *d = *dd;
		pstate->xload += d->load;
		if (d->power)
			pstate->flags |= FLAG_ACTIVE;
		if (d->state != Standby)
			pstate->flags &= ~FLAG_ALL_STANDBY;
	}
	pstate->xload *= -1;

	// deviation of calculated load to actual load in %
	pstate->dxload = (pstate->xload - pstate->load) * 100 / pstate->xload;

	// indicate standby check when deviation between actual load and calculated load is three times above 33%
	if (pstate->dxload > 33 && s1->dxload > 33 && s2->dxload > 33)
		pstate->flags |= FLAG_CHECK_STANDBY;

	// greedy power = akku + grid
	pstate->greedy = (pstate->grid + pstate->akku) * -1;
	if (pstate->greedy > 0)
		pstate->greedy -= NOISE; // threshold for ramp up - allow small akku charging
	if (abs(pstate->greedy) < NOISE)
		pstate->greedy = 0;
	if (!PSTATE_ACTIVE && pstate->greedy < 0)
		pstate->greedy = 0; // no active devices - nothing to ramp down

	// modest power = only grid
	pstate->modest = pstate->grid * -1; // no threshold - try to regulate around grid=0
	if (pstate->greedy < pstate->modest)
		pstate->modest = pstate->greedy; // greedy cannot be smaller than modest
	if (abs(pstate->modest) < NOISE)
		pstate->modest = 0;
	if (!PSTATE_ACTIVE && pstate->modest < 0)
		pstate->modest = 0; // no active devices - nothing to ramp down

	// ramp on grid download or akku discharge or when we have greedy/modest power
	if (pstate->akku > NOISE || pstate->grid > NOISE || pstate->greedy || pstate->modest)
		pstate->flags |= FLAG_RAMP;

	// clear RAMP flag when values not valid
	int sum = pstate->grid + pstate->akku + pstate->load + pstate->pv;
	if (abs(sum) > SUSPICIOUS) {
		xdebug("FRONIUS suspicious values detected: sum=%d", sum); // probably inverter power dissipations (?)
//		pstate->flags &= ~FLAG_RAMP;
	}
	if (pstate->load > 0) {
		xdebug("FRONIUS positive load detected");
//		pstate->flags &= ~FLAG_RAMP;
	}
	if (pstate->grid < -NOISE && pstate->akku > NOISE) {
		int waste = abs(pstate->grid) < pstate->akku ? abs(pstate->grid) : pstate->akku;
		xdebug("FRONIUS wasting %d akku -> grid power", waste);
		pstate->flags &= ~FLAG_RAMP;
	}
	if (pstate->dgrid > 500) { // e.g. refrigerator starts !!!
		xdebug("FRONIUS grid spike detected %d: %d -> %d", pstate->grid - s1->grid, s1->grid, pstate->grid);
		pstate->flags &= ~FLAG_RAMP;
	}
	if (!potd) {
		xlog("FRONIUS no potd selected!");
		pstate->flags &= ~FLAG_RAMP;
	}
	if (!f10->active) {
		xlog("FRONIUS Fronius10 is not active!");
		pstate->flags &= ~FLAG_RAMP;
	}
	if (!f7->active) {
		xlog("FRONIUS Fronius7 is not active!");
	}
}

static void emergency() {
	xlog("FRONIUS emergency shutdown at %d akku discharge / %d grid download", pstate->akku, pstate->grid);
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

static void daily(time_t now_ts) {
	xlog("FRONIUS executing daily tasks...");

	// aggregate 24 pstate hours into one day
	pstate_t pd;
	aggregate_table((int*) &pd, (int*) pstate_hours, PSTATE_SIZE, 24);
	dump_table((int*) pstate_hours, PSTATE_SIZE, 24, -1, "FRONIUS pstate_hours", PSTATE_HEADER);
	dump_struct((int*) &pd, PSTATE_SIZE, "[ØØ]", 0);

	// aggregate 24 gstate hours into one day
	gstate_t gd;
	aggregate_table((int*) &gd, (int*) gstate_hours, GSTATE_SIZE, 24);
	dump_table((int*) gstate_hours, GSTATE_SIZE, 24, -1, "FRONIUS gstate_hours", GSTATE_HEADER);
	dump_struct((int*) &gd, GSTATE_SIZE, "[ØØ]", 0);

	// copy tomorrow forecasts to today
	// TODO needed?
	mosmix_takeover();
	mosmix_dump_today(-1);

	// store to disk
#ifndef FRONIUS_MAIN
	store_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));
	store_blob(COUNTER_FILE, counter_hours, sizeof(counter_hours));
	store_blob(PSTATE_H_FILE, pstate_hours, sizeof(pstate_hours));
	store_blob(PSTATE_M_FILE, pstate_minutes, sizeof(pstate_minutes));
	mosmix_store_state();
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

	// copy counters to next hour (Fronius7 goes into sleep mode - no updates overnight)
	counter_t *c1 = get_counter_hour(1);
	memcpy(c1, (void*) counter, sizeof(counter_t));

	// aggregate 59 minutes into current hour
	// dump_table((int*) pstate_minutes, PSTATE_SIZE, 60, -1, "FRONIUS pstate_minutes", PSTATE_HEADER);
	pstate_t *ph = get_pstate_hour(0);
	aggregate_table((int*) ph, (int*) pstate_minutes, PSTATE_SIZE, 60);
	// dump_struct((int*) ph, PSTATE_SIZE, "[ØØ]", 0);

	// recalculate gstate, mosmix, then choose storage strategy potd
	calculate_gstate();
	calculate_mosmix(now_ts);
	storage_strategy();
	choose_program();

	// print actual gstate
	dump_table((int*) gstate_hours, GSTATE_SIZE, 24, now->tm_hour, "FRONIUS gstate_hours", GSTATE_HEADER);
	print_gstate(NULL);
}

static void minly(time_t now_ts) {
	// xlog("FRONIUS executing minutely tasks...");

	// aggregate 59 seconds into current minute
	// dump_table((int*) pstate_seconds, PSTATE_SIZE, 60, -1, "FRONIUS pstate_seconds", PSTATE_HEADER);
	pstate_t *pm = get_pstate_minute(0);
	aggregate_table((int*) pm, (int*) pstate_seconds, PSTATE_SIZE, 60);
	// dump_struct((int*) pm, PSTATE_SIZE, "[ØØ]", 0);

	// clear sum counters
	pstate->sdpv = pstate->sdgrid = pstate->sdload = 0;
}

static void fronius() {
	time_t now_ts;
	device_t *device = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// the FRONIUS main loop
	while (1) {

		// get actual time and make a copy
		now_ts = time(NULL);
		localtime(&now_ts);
		memcpy(now, lt, sizeof(*lt));

		// update state and counter pointers
		counter = get_counter_hour(0);
		gstate = get_gstate_hour(0);
		pstate = get_pstate_second(0);

		// wait till this second is over, meanwhile sunspec threads have values updated
		while (now_ts == time(NULL))
			msleep(333);

		// initialize program of the day if not yet done and choose storage strategy
		if (!potd) {
			calculate_pstate();
			calculate_gstate();
			calculate_mosmix(now_ts);
			storage_strategy();
			choose_program();
			continue;
		}

		// calculate new pstate
		calculate_pstate();

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

		// prio2: perform standby check logic
		if (!device)
			device = standby();

		// prio3: check if higher priorized device can steal from lower priorized
		if (!device)
			device = steal();

		// prio4: ramp up/down
		if (!device && PSTATE_RAMP)
			device = ramp();

		// print combined device and pstate when we had delta or device action
		if (PSTATE_DELTA || device)
			print_state(device);

		// minutely tasks
		if (now->tm_sec == 59) {
			minly(now_ts);

			// hourly tasks
			if (now->tm_min == 59) {
				hourly(now_ts);

				// daily tasks
				if (now->tm_hour == 23)
					daily(now_ts);
			}
		}
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

	init_all_devices();

	// create a sunspec handle and remove models not needed
	sunspec_t *ss = sunspec_init("Meter", "192.168.25.230", 200);
	sunspec_read(ss);
	ss->common = 0;

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

	// average offset power at start
	printf("calculating offset start");
	for (int i = 0; i < 10; i++) {
		sunspec_read(ss);
		grid = SFI(ss->meter->W, ss->meter->W_SF);
		printf(" %d", grid);
		offset_start += grid;
		sleep(1);
	}
	offset_start = offset_start / 10 + (offset_start % 10 < 5 ? 0 : 1);
	printf(" --> average %d\n", offset_start);
	sleep(5);

	// do a full drive over SSR characteristic load curve from cold to hot and capture power
	printf("starting measurement with maximum power %d watt 1%%=%d watt\n", max_power, onepercent);
	for (int i = 0; i < 1000; i++) {
		voltage = i * 10;
		snprintf(message, 16, "v:%d:%d", voltage, 0);
		sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
		int ms = 200 < i && i < 800 ? 1000 : 100; // slower between 2 and 8 volt
		msleep(ms);
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
	offset_end = offset_end / 10 + (offset_end % 10 < 5 ? 0 : 1);
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
		printf("target power %04d closest %04d range +/-5 watt around closest: ", target, measure[closest]);
		for (int j = 0; j < 1000; j++)
			if (measure[closest] - 5 < measure[j] && measure[j] < measure[closest] + 5) {
				printf(" %d:%d", measure[j], j * 10);
				sum += j * 10;
				count++;
			}

		// average of all closest voltages
		if (count) {
			int z = (sum * 10) / count;
			raster[i] = z / 10 + (z % 10 < 5 ? 0 : 1);
		}
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
	printf("\nwaiting 60s for cool down\n");
	sleep(60);
	for (int i = 0; i <= 100; i++) {
		snprintf(message, 16, "v:%d:%d", raster[i], 0);
		sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
		msleep(2000);
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

static int init() {
	set_debug(1);

	init_all_devices();
	set_all_devices(0);

	ZERO(pstate_seconds);
	ZERO(pstate_minutes);
	ZERO(pstate_hours);
	ZERO(gstate_hours);
	ZERO(counter_hours);

	load_blob(PSTATE_M_FILE, pstate_minutes, sizeof(pstate_minutes));
	load_blob(PSTATE_H_FILE, pstate_hours, sizeof(pstate_hours));
	load_blob(COUNTER_FILE, counter_hours, sizeof(counter_hours));
	load_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));
	mosmix_load_state();

	meter = sunspec_init_poll("Meter", "192.168.25.230", 200, &update_meter);
	f10 = sunspec_init_poll("Fronius10", "192.168.25.230", 1, &update_f10);
	f7 = sunspec_init_poll("Fronius7", "192.168.25.231", 2, &update_f7);

	// wait for collecting models
	sleep(3);

	// initialize hourly & daily & monthly
	time_t now_ts = time(NULL);
	lt = localtime(&now_ts);
	memcpy(now, lt, sizeof(*lt));

	return 0;
}

static void stop() {
	sunspec_stop(f7);
	sunspec_stop(f10);
	sunspec_stop(meter);

#ifndef FRONIUS_MAIN
	store_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));
	store_blob(COUNTER_FILE, counter_hours, sizeof(counter_hours));
	store_blob(PSTATE_H_FILE, pstate_hours, sizeof(pstate_hours));
	store_blob(PSTATE_M_FILE, pstate_minutes, sizeof(pstate_minutes));
	mosmix_store_state();
#endif

	if (sock)
		close(sock);
}

// do all calculations in one single round trip and exit
static int single() {
	time_t now_ts = time(NULL);
	init();

	gstate = get_gstate_hour(0);
	counter = get_counter_hour(0);
	pstate = get_pstate_second(0);
	sleep(1); // update values

	calculate_pstate();
	calculate_gstate();
	calculate_mosmix(now_ts);
	storage_strategy();
	choose_program();

	// aggregate 24 pstate hours into one day
	pstate_t pd;
	aggregate_table((int*) &pd, (int*) pstate_hours, PSTATE_SIZE, 24);
	dump_table((int*) pstate_hours, PSTATE_SIZE, 24, -1, "FRONIUS pstate_hours", PSTATE_HEADER);
	dump_struct((int*) &pd, PSTATE_SIZE, "[ØØ]", 0);
	print_state(NULL);

	// aggregate 24 gstate hours into one day
	gstate_t gd;
	aggregate_table((int*) &gd, (int*) gstate_hours, GSTATE_SIZE, 24);
	dump_table((int*) gstate_hours, GSTATE_SIZE, 24, -1, "FRONIUS gstate_hours", GSTATE_HEADER);
	dump_struct((int*) &gd, GSTATE_SIZE, "[ØØ]", 0);
	print_gstate(NULL);

	stop();
	return 0;
}

// fake state and counter records from actual values and copy to history records
static int fake() {
	counter_t c;
	gstate_t g;
	pstate_t p;

	counter = &c;
	gstate = &g;
	pstate = &p;

	ZEROP(counter);
	ZEROP(gstate);
	ZEROP(pstate);

	init();
	sleep(3);
	stop();

	calculate_pstate();
	calculate_gstate();

	for (int i = 0; i < 24; i++)
		memcpy(&counter_hours[i], (void*) counter, sizeof(counter_t));
	for (int i = 0; i < 24; i++)
		memcpy(&gstate_hours[i], (void*) gstate, sizeof(gstate_t));
	for (int i = 0; i < 24; i++)
		memcpy(&pstate_hours[i], (void*) pstate, sizeof(pstate_t));
	for (int i = 0; i < 60; i++)
		memcpy(&pstate_minutes[i], (void*) pstate, sizeof(pstate_t));

	store_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));
	store_blob(COUNTER_FILE, counter_hours, sizeof(counter_hours));
	store_blob(PSTATE_H_FILE, pstate_hours, sizeof(pstate_hours));
	store_blob(PSTATE_M_FILE, pstate_minutes, sizeof(pstate_minutes));

	return 0;
}

// sample grid load from meter
static int grid() {
	sunspec_t *ss = sunspec_init("Meter", "192.168.25.230", 200);
	sunspec_read(ss);
	ss->common = 0;

	float grid;
	while (1) {
		sunspec_read(ss);
		grid = SFF(ss->meter->W, ss->meter->W_SF);
		printf("%.1f\n", grid);
		msleep(666);
	}

	return 0;
}

// set charge(-) / discharge(+) limits or reset when 0
static int battery(char *arg) {
	sunspec_t *ss = sunspec_init("Fronius10", "192.168.25.230", 1);
	sunspec_read(ss);

	int wh = atoi(arg);
	if (wh > 0)
		return sunspec_storage_limit_discharge(ss, wh);
	if (wh < 0)
		return sunspec_storage_limit_charge(ss, wh * -1);
	return sunspec_storage_limit_reset(ss);
}

// set minimum SoC
static int minimum(char *arg) {
	sunspec_t *ss = sunspec_init("Fronius10", "192.168.25.230", 1);
	sunspec_read(ss);

	int min = atoi(arg);
	return sunspec_storage_minimum_soc(ss, min);
}

static int test() {
	return 0;
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

	int c;
	while ((c = getopt(argc, argv, "b:c:m:o:fgst")) != -1) {
		// printf("getopt %c\n", c);
		switch (c) {
		case 'b':
			// -1: charge only, 1: discharge only, 0: charge and discharge
			return battery(optarg);
		case 'c':
			// execute as: stdbuf -i0 -o0 -e0 ./fronius -c boiler1 > boiler1.txt
			return calibrate(optarg);
		case 'm':
			return minimum(optarg);
		case 'o':
			return fronius_override(optarg);
		case 'f':
			return fake();
		case 'g':
			return grid();
		case 's':
			return single();
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
