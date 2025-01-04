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

#define MIN_SOC					(SFI(f10->storage->MinRsvPct, f10->storage->MinRsvPct_SF) * 10)
#define AKKU_CAPACITY			(SFI(f10->nameplate->WRtg, f10->nameplate->WRtg_SF))
#define AKKU_CAPACITY_SOC(soc)	(AKKU_CAPACITY * soc / 1000)
#define EMERGENCY				(AKKU_CAPACITY / 10)

#define WAIT_NEXT_RAMP			1
#define WAIT_RESPONSE			3
#define WAIT_NEXT				1
#define WAIT_AKKU_CHARGE		30
#define WAIT_AKKU_RAMP			10

#define MOSMIX3X24				"FRONIUS mosmix Rad1h/SunD1/RSunD today %d/%d/%d tomorrow %d/%d/%d tomorrow+1 %d/%d/%d"

#define DD						(*dd)
#define UP						(*dd)->total
#define DOWN					(*dd)->total * -1

// program of the day - choosen by mosmix forecast data
static potd_t *potd = 0;

// counter history every hour over one day and access pointers
static counter_t counter_hours[24];
static volatile counter_t *counter = 0;
#define COUNTER_NOW				(&counter_hours[now->tm_hour])
#define COUNTER_LAST			(&counter_hours[now->tm_hour > 00 ? now->tm_hour - 1 : 23])
#define COUNTER_NEXT			(&counter_hours[now->tm_hour < 23 ? now->tm_hour + 1 : 00])

// 24h slots over one week and access pointers
static gstate_t gstate_hours[24 * 7];
static volatile gstate_t *gstate = 0;
#define GSTATE_NOW				(&gstate_hours[24 * now->tm_wday + now->tm_hour])
#define GSTATE_LAST				(&gstate_hours[24 * now->tm_wday - (now->tm_hour > 0 ? now->tm_hour - 1 : 23)])
#define GSTATE_HOUR(h)			(&gstate_hours[24 * now->tm_wday + h])
#define GSTATE_TODAY			GSTATE_HOUR(0)

// pstate history every second/minute/hour and access pointers
static pstate_t pstate_seconds[60], pstate_minutes[60], pstate_hours[24];
static volatile pstate_t *pstate = 0;
#define PSTATE_NOW				(&pstate_seconds[now->tm_sec])
#define PSTATE_SEC_LAST1		(&pstate_seconds[now->tm_sec > 0 ? now->tm_sec - 1 : 59])
#define PSTATE_SEC_LAST2		(&pstate_seconds[now->tm_sec > 1 ? now->tm_sec - 2 : 58])
#define PSTATE_SEC_LAST3		(&pstate_seconds[now->tm_sec > 2 ? now->tm_sec - 3 : 57])
#define PSTATE_MIN_NOW			(&pstate_minutes[now->tm_min])
#define PSTATE_MIN_LAST1		(&pstate_minutes[now->tm_min > 0 ? now->tm_min - 1 : 59])
#define PSTATE_MIN_LAST2		(&pstate_minutes[now->tm_min > 1 ? now->tm_min - 2 : 58])
#define PSTATE_HOUR_NOW			(&pstate_hours[now->tm_hour])
#define PSTATE_HOUR(h)			(&pstate_hours[h])

// SunSpec modbus devices
static sunspec_t *f10 = 0, *f7 = 0, *meter = 0;

static struct tm *lt, now_tm, *now = &now_tm;
static int sock = 0;

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

static int akku_standby(device_t *akku) {
	if (akku->state == Standby)
		return 0; // continue loop

	xdebug("FRONIUS set akku STANDBY");
#ifndef FRONIUS_MAIN
	sunspec_storage_limit_both(f10, 0, 0);
#endif
	akku->state = Standby;
	akku->timer = WAIT_RESPONSE;
	akku->power = akku->load = akku->xload = 0; // disable response/standby/steal logic
	return 0; // continue loop
}

static int akku_charge(device_t *akku) {
	if (akku->state == Charge)
		return 0; // continue loop

	// enable charging
	xdebug("FRONIUS set akku CHARGE");
#ifndef FRONIUS_MAIN
	sunspec_storage_limit_discharge(f10, 0);
#endif
	akku->state = Charge;
	akku->timer = WAIT_AKKU_CHARGE;
	akku->load = akku->xload = 0; // disable response/standby/steal logic
	akku->power = 1;
	return 1; // loop done
}

static int akku_discharge(device_t *akku) {
	if (akku->state == Discharge)
		return 0; // continue loop

#ifndef FRONIUS_MAIN
	// enable discharge
	int limit = WINTER && (gstate->survive < 10 || gstate->tomorrow < 10000);
	if (limit) {
		xdebug("FRONIUS set akku DISCHARGE limit BASELOAD");
		sunspec_storage_limit_both(f10, 0, BASELOAD);
	} else {
		xdebug("FRONIUS set akku DISCHARGE");
		sunspec_storage_limit_charge(f10, 0);
	}

	// winter mode
	if (WINTER)
		sunspec_storage_minimum_soc(f10, 10);
	else
		sunspec_storage_minimum_soc(f10, 5);
#endif

	akku->state = Discharge;
	akku->timer = WAIT_RESPONSE;
	akku->power = akku->load = akku->xload = 0; // disable response/standby/steal logic
	return 1; // loop done
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

static int collect_pstate_load(int from, int hours) {
	int load = 0;
	char line[LINEBUF], value[25];
	strcpy(line, "FRONIUS mosmix load");
	for (int i = 0; i < hours; i++) {
		int hour = from + i;
		if (hour >= 24)
			hour -= 24;
		int hload = PSTATE_HOUR(hour)->load * -1;
		load += hload;
		snprintf(value, 25, " %d:%d", hour, hload);
		strcat(line, value);
	}
	xdebug(line);
	return load;
}

static int boiler_send(device_t *boiler, int power) {
	if (boiler->addr == NULL)
		return 0;

	// send message to boiler
	char message[16];
	snprintf(message, 16, "p:%d:%d", power, 0);

#ifndef FRONIUS_MAIN
	// write IP and port into sockaddr structure
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(boiler->addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	int ret = sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	if (ret < 0)
		return xerrr(0, "Sendto failed on %s %s", boiler->addr, strerror(ret));
#endif

	int step = power - boiler->power;
	if (step < 0)
		xdebug("FRONIUS ramp↓ %s step %d UDP %s", boiler->name, step, message);
	else
		xdebug("FRONIUS ramp↑ %s step +%d UDP %s", boiler->name, step, message);

	// update power values
	boiler->power = power;
	boiler->load = boiler->total * boiler->power / 100;
	boiler->xload = boiler->total * step / -100;
	boiler->aload = pstate ? pstate->load : 0;
	boiler->timer = WAIT_RESPONSE;
	return 1; // loop done
}

static int check_override(device_t *d, int power) {
	if (d->override) {
		time_t t = time(NULL);
		if (t > d->override) {
			xdebug("FRONIUS Override expired for %s", d->name);
			d->override = 0;
			power = 0;
		} else {
			xdebug("FRONIUS Override active for %lu seconds on %s", d->override - t, d->name);
			power = d->adj ? 100 : 1;
		}
	}
	return power;
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
	xlogl_float(line, 1, gstate->heating < 10, "Heating", FLOAT10(gstate->heating));
	strcat(line, " potd:");
	strcat(line, potd ? potd->name : "NULL");
	xlogl_end(line, strlen(line), message);
}

static void print_state(device_t *d) {
	char line[512], value[16]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS");

	for (device_t **dd = potd->devices; *dd; dd++) {
		if (DD->adj)
			snprintf(value, 5, " %3d", DD->power);
		else
			snprintf(value, 5, "   %c", DD->power ? 'X' : '_');
		strcat(line, value);
	}

	strcat(line, "   state ");
	for (device_t **dd = potd->devices; *dd; dd++) {
		snprintf(value, 5, "%d", DD->state);
		strcat(line, value);
	}

	strcat(line, "   nores ");
	for (device_t **dd = potd->devices; *dd; dd++) {
		snprintf(value, 5, "%d", DD->noresponse);
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
	xlogl_int(line, 1, 0, "Ramp", pstate->ramp);
	xlogl_int(line, 0, 0, "Load", pstate->load);
	xlogl_float(line, 0, 0, "SoC", FLOAT10(pstate->soc));

	if (d)
		xlogl_int(line, 0, 0, d->name, d->timer);
	xlogl_end(line, strlen(line), 0);
}

// call device specific ramp function
static int ramp_device(device_t *d, int power) {
	if (power < 0)
		xdebug("FRONIUS ramp↓ %d %s", power, d->name);
	else if (power > 0)
		xdebug("FRONIUS ramp↑ +%d %s", power, d->name);
	else
		xdebug("FRONIUS ramp 0 %s", d->name);
	return (d->ramp)(d, power);
}

static int select_program(const potd_t *p) {
	if (potd == p)
		return 0;

	// potd has changed - reset all devices
	a1.power = -1;
	for (device_t **dd = DEVICES; *dd; dd++)
		ramp_device(DD, DOWN);

	xlog("FRONIUS selecting %s program of the day", p->name);
	potd = (potd_t*) p;

	return 0;
}

// choose program of the day
static int choose_program() {
	if (!gstate)
		return select_program(&MODEST);

	// we will NOT survive - charging akku has priority
	if (gstate->survive < 10)
		return select_program(&MODEST);

	// tomorrow not enough pv - charging akku has priority
	if (gstate->tomorrow < AKKU_CAPACITY)
		return select_program(&MODEST);

	// tomorrow more pv than today - charge akku tommorrow
	if (gstate->tomorrow > gstate->today)
		return select_program(&GREEDY);

	// enough pv available for survive + heating
	if (gstate->heating > 10)
		return select_program(&PLENTY);

	return select_program(&MODEST);
}

// minimum available power for ramp up
static int rampup_min(device_t *d) {
	int min = d->adj ? d->total / 100 : d->total; // adjustable: 1% of total, dumb: total
	if (pstate->soc < 1000)
		min += min / 10; // 10% more while akku is charging to avoid excessive grid load
	if (PSTATE_DISTORTION)
		min *= 2; // 100% more on distortion
	return min;
}

static device_t* rampup(int power) {
	for (device_t **dd = potd->devices; *dd; dd++)
		if (ramp_device(DD, power))
			return DD;

	return 0;
}

static device_t* rampdown(int power) {
	// jump to last entry
	device_t **dd = potd->devices;
	while (*dd)
		dd++;

	// now go backward - this gives reverse order
	while (dd-- != potd->devices)
		if (ramp_device(DD, power))
			return DD;

	return 0;
}

static device_t* ramp() {
	device_t *d;

	// prio1: ramp down in reverse order
	if (pstate->ramp < 0) {
		d = rampdown(pstate->ramp);
		if (d)
			return d;
	}

	// ramp up only when state is stable or enough power
	int ok = PSTATE_STABLE || pstate->ramp > 1000;
	if (!ok)
		return 0;

	// prio2: ramp up in order
	if (pstate->ramp > 0) {
		d = rampup(pstate->ramp);
		if (d)
			return d;
	}

	return 0;
}

static int steal_thief_victim(device_t *t, device_t *v) {
	// thief not active or already on or nothing to steal from victim
	if (t->state != Active || t->power || !v->load)
		return 0;

	// we can steal akkus charge charge power or victims load
	int steal = v == &a1 ? pstate->akku * -1 : v->load;
	int power = pstate->ramp + steal;

	// not enough to steal
	int min = rampup_min(t);
	if (power < min)
		return 0;

	xdebug("FRONIUS steal %d from %s and provide it to %s with a load of %d", steal, v, v->name, t, t->name, t->total);

	// ramp down victim, akku ramps down itself
	if (v != &a1)
		ramp_device(v, v->load * -1);
	ramp_device(t, power);

	// give akku time to adjust
	if (v == &a1)
		t->timer = WAIT_AKKU_RAMP;

	// no response expected as we put power from one to another device
	t->xload = 0;
	return 1;
}

static device_t* steal() {
	device_t **tail = potd->devices;

	// jump to end
	while (*tail)
		tail++;
	tail--;

	// thief goes forward, victim backward till it reaches thief
	for (device_t **tt = potd->devices; *tt != 0; tt++)
		for (device_t **vv = tail; vv != tt; vv--)
			if (steal_thief_victim(*tt, *vv))
				return *tt;

	return 0;
}

static device_t* perform_standby(device_t *d) {
	d->state = Standby_Check;
	int power = pstate->pv / (d->power > 0 ? -2 : 2);
	xdebug("FRONIUS starting standby check on %s with power=%d", d->name, power);
	ramp_device(d, power);
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
		for (device_t **dd = DEVICES; *dd; dd++) {
			if (!DD->adj && DD->state == Active) {
				ramp_device(DD, DOWN);
				DD->state = Standby;
			}
		}
	}

	// no standby check indicated or state is not stable
	if (!PSTATE_CHECK_STANDBY || !PSTATE_STABLE)
		return 0;

	// try first active powered device with noresponse counter > 0
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD->state == Active && DD->power && DD->noresponse > 0)
			return perform_standby(DD);

	// try first active powered adjustable device
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD->state == Active && DD->power && DD->adj)
			return perform_standby(DD);

	// try first active powered device
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD->state == Active && DD->power)
			return perform_standby(DD);

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
		d->timer = WAIT_NEXT_RAMP;
		return d;
	}

	// standby check was negative - we got a response
	if (d->state == Standby_Check && response) {
		xdebug("FRONIUS standby check negative for %s, delta load expected %d actual %d", d->name, expected, delta);
		d->noresponse = 0;
		d->state = Active;
		d->timer = WAIT_NEXT_RAMP;
		return d;
	}

	// standby check was positive -> set device into standby
	if (d->state == Standby_Check && !response) {
		xdebug("FRONIUS standby check positive for %s, delta load expected %d actual %d --> entering standby", d->name, expected, delta);
		ramp_device(d, d->total * -1);
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

static void calculate_mosmix() {
	// update forecasts
	if (mosmix_load(MARIENBERG))
		return;

	// update produced energy this hour and recalculate mosmix factors for each mppt
	mosmix_mppt(now, gstate->mppt1, gstate->mppt2, gstate->mppt3, gstate->mppt4);

	// calculate expected
	int today, tomorrow, sod, eod;
	mosmix_expected(now, &today, &tomorrow, &sod, &eod);
	gstate->today = today;
	gstate->tomorrow = tomorrow;
	gstate->expected = eod;

	// mosmix 24h forecasts today, tomorrow and tomorrow+1
	mosmix_csv_t m0, m1, m2;
	mosmix_24h(0, &m0);
	mosmix_24h(1, &m1);
	mosmix_24h(2, &m2);
	xdebug(MOSMIX3X24, m0.Rad1h, m0.SunD1, m0.RSunD, m1.Rad1h, m1.SunD1, m1.RSunD, m2.Rad1h, m2.SunD1, m2.RSunD);

	// calculate survival factor
	int hours, from, to;
	mosmix_survive(now, BASELOAD / 2, &hours, &from, &to);
	int available = gstate->expected + gstate->akku;
	int needed = collect_pstate_load(from, hours);
	float survive = needed ? (float) available / (float) needed : 0.0;
	gstate->survive = survive * 10; // store as x10 scaled
	xdebug("FRONIUS survive needed=%d available=%d (%d expected + %d akku) --> %.2f", needed, available, gstate->expected, gstate->akku, survive);

	// calculate heating factor
	// TODO auto collect heating power from devices
	// TODO nochmal überdenken
	mosmix_heating(now, 1500, &hours, &from, &to);
	needed += 1500 * hours; // survive + heating
	float heating = needed ? (float) available / (float) needed : 0.0;
	gstate->heating = heating * 10; // store as x10 scaled
	xdebug("FRONIUS heating needed=%d available=%d (%d expected + %d akku) --> %.2f", needed, available, gstate->expected, gstate->akku, heating);

	// actual vs. yesterdays expected ratio
	int actual = 0;
	for (int i = 0; i <= now->tm_hour; i++)
		actual += GSTATE_HOUR(i)->pv;
	int yesterdays_tomorrow = GSTATE_HOUR(23)->tomorrow;
	float error = yesterdays_tomorrow ? (float) actual / (float) yesterdays_tomorrow : 0;
	xdebug("FRONIUS yesterdays forecast for today %d, actual %d, error %.2f", yesterdays_tomorrow, actual, error);

	// dump tables
	mosmix_dump_today(now);
	mosmix_dump_tomorrow(now);
}

static void calculate_gstate() {
	// take over SoC
	gstate->soc = pstate->soc;

	// get previous values to calculate deltas
	gstate_t *g = GSTATE_LAST;
	counter_t *c = COUNTER_LAST;

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

// shape pstate values below NOISE
static void shape_pstate() {
	if (abs(pstate->dgrid) < NOISE)
		pstate->dgrid = 0;
	if (abs(pstate->dload) < NOISE)
		pstate->dload = 0;
	if (abs(pstate->akku) < NOISE)
		pstate->akku = 0;
//	if (abs(pstate->grid) < NOISE)
//		pstate->grid = 0;
}

static int tendence() {
	if (PSTATE_MIN_LAST1->dpv < 0 && PSTATE_MIN_LAST2->dpv < 0)
		return -1;
	if (PSTATE_MIN_LAST1->dpv > 0 && PSTATE_MIN_LAST2->dpv > 0)
		return 1;
	return 0;
}

static void calculate_pstate() {
	// clear all flags
	pstate->flags = 0;

	// get history states
	pstate_t *m1 = PSTATE_MIN_LAST1;
	pstate_t *m2 = PSTATE_MIN_LAST2;
	pstate_t *s1 = PSTATE_SEC_LAST1;
	pstate_t *s2 = PSTATE_SEC_LAST2;
	pstate_t *s3 = PSTATE_SEC_LAST3;

	// total PV produced by both inverters
	pstate->pv = pstate->mppt1 + pstate->mppt2 + pstate->mppt3 + pstate->mppt4;
	pstate->dpv = pstate->pv - s1->pv;
	pstate->sdpv = s1->sdpv + abs(pstate->dpv);

	// grid, delta grid and sum
	pstate->dgrid = pstate->grid - s1->grid;
	pstate->sdgrid = s1->sdgrid + abs(pstate->dgrid);

	// calculate load, delta load + sum
	pstate->load = (pstate->ac10 + pstate->ac7 + pstate->grid) * -1;
	pstate->dload = pstate->load - s1->load;
	pstate->sdload = s1->sdload + abs(pstate->dload);

	// check if we have delta ac power anywhere
	if (abs(pstate->grid - s1->grid) > NOISE)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac10 - s1->ac10) > NOISE)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac7 - s1->ac7) > NOISE)
		pstate->flags |= FLAG_DELTA;

	// akku power is Fronius10 DC power minus PV
	pstate->akku = pstate->dc10 - (pstate->mppt1 + pstate->mppt2);

	// offline mode when 3x not enough PV production
	if (pstate->pv < NOISE && s1->pv < NOISE && s2->pv < NOISE && s3->pv < NOISE) {
		int burnout_time = !SUMMER && (now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8);
		int burnout_possible = TEMP_IN < 20 && pstate->soc > 150;
		if (burnout_time && burnout_possible && AKKU_BURNOUT)
			pstate->flags |= FLAG_BURNOUT; // akku burnout between 6 and 9 o'clock when possible
		else
			pstate->flags |= FLAG_OFFLINE; // offline
		pstate->ramp = pstate->xload = pstate->dxload = pstate->pv = pstate->dpv = pstate->mppt1 = pstate->mppt2 = pstate->mppt3 = pstate->mppt4 = 0;
		shape_pstate();
		return;
	}

	// emergency shutdown when last minute extreme akku discharge or grid download
	int emergency = EMERGENCY;
	if (emergency && (m1->akku > emergency || m1->grid > emergency)) {
		pstate->flags |= FLAG_EMERGENCY;
		return;
	}

	// state is stable when we have three times no grid changes
	if (!s1->dgrid && !s2->dgrid && !s3->dgrid)
		pstate->flags |= FLAG_STABLE;

	// distortion when current sdpv is too big or aggregated last two sdpv's are too big
	int d0 = pstate->pv && (pstate->sdpv / pstate->pv) >= 2;
	int d1 = m1->pv && (m1->sdpv / m1->pv) >= 2;
	int d2 = m2->pv && (m2->sdpv / m2->pv) >= 2;
	if (d0 || d1 || d2) {
		pstate->flags |= FLAG_DISTORTION;
		xdebug("FRONIUS distortion=%d d0=%d/%d d1=%d/%d d2=%d/%d", PSTATE_DISTORTION, pstate->sdpv, pstate->pv, m1->sdpv, m1->pv, m2->sdpv, m2->pv);
	}

	// device loop:
	// - expected load
	// - active devices
	// - all devices in standby
	pstate->flags |= FLAG_ALL_STANDBY;
	pstate->xload = BASELOAD;
	for (device_t **dd = DEVICES; *dd; dd++) {
		pstate->xload += DD->load;
		if (DD->power > 0 && DD != &a1) // excl. akku; -1 when unitialized!
			pstate->flags |= FLAG_ACTIVE;
		if (DD->state != Standby)
			pstate->flags &= ~FLAG_ALL_STANDBY;
	}
	pstate->xload *= -1;

	// deviation of calculated load to actual load in %
	pstate->dxload = (pstate->xload - pstate->load) * 100 / pstate->xload;

	// calculate ramp up/down power
	pstate->ramp = pstate->grid * -1 - RAMP_OFFSET;
	if (-RAMP_WINDOW < pstate->ramp && pstate->ramp < RAMP_WINDOW) // stable between 0..50
		pstate->ramp = 0;
	if (pstate->akku < -NOISE && -RAMP_WINDOW < pstate->grid && pstate->grid < RAMP_WINDOW)
		pstate->ramp = 0; // akku is regulating around 0
	if (pstate->ramp < 0 && !PSTATE_ACTIVE && a1.state == Discharge)
		pstate->ramp = 0; // nothing to ramp down when no active devices and akku is in Discharge mode

	// indicate standby check when we have enoug power and deviation between actual load and calculated load is three times above 33%
	if (pstate->ramp > BASELOAD * 2 && s1->dxload > 33 && s2->dxload > 33 && s3->dxload > 33)
		pstate->flags |= FLAG_CHECK_STANDBY;

	// ramp on grid download or akku discharge or when we have ramp power
	if (pstate->akku > NOISE || pstate->grid > NOISE || pstate->ramp)
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
	if (pstate->dgrid > BASELOAD * 2) { // e.g. refrigerator starts !!!
		xdebug("FRONIUS grid spike detected %d: %d -> %d", pstate->grid - s1->grid, s1->grid, pstate->grid);
		pstate->flags &= ~FLAG_RAMP;
	}
	if (!f10->active) {
		xlog("FRONIUS Fronius10 is not active!");
		pstate->flags &= ~FLAG_RAMP;
	}
	if (!f7->active) {
//		xlog("FRONIUS Fronius7 is not active!");
	}

	shape_pstate();
}

static void emergency() {
	xlog("FRONIUS emergency shutdown at akku=%d grid=%d ", pstate->akku, pstate->grid);
	for (device_t **dd = DEVICES; *dd; dd++)
		ramp_device(DD, DOWN);
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
	aggregate_table((int*) &gd, (int*) GSTATE_TODAY, GSTATE_SIZE, 24);
	dump_table((int*) GSTATE_TODAY, GSTATE_SIZE, 24, -1, "FRONIUS gstate_hours", GSTATE_HEADER);
	dump_struct((int*) &gd, GSTATE_SIZE, "[ØØ]", 0);

	// store to disk
	// TODO csv
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
	for (device_t **dd = DEVICES; *dd; dd++) {
		DD->noresponse = 0;
		if (DD->state == Standby)
			DD->state = Active;
	}

	// force all devices off when offline
	if (PSTATE_OFFLINE)
		for (device_t **dd = DEVICES; *dd; dd++)
			ramp_device(DD, DOWN);

	// copy counters to next hour (Fronius7 goes into sleep mode - no updates overnight)
	memcpy(COUNTER_NEXT, (void*) counter, sizeof(counter_t));

	// aggregate 59 minutes into current hour
	// dump_table((int*) pstate_minutes, PSTATE_SIZE, 60, -1, "FRONIUS pstate_minutes", PSTATE_HEADER);
	aggregate_table((int*) PSTATE_HOUR_NOW, (int*) pstate_minutes, PSTATE_SIZE, 60);
	// dump_struct((int*) PSTATE_HOUR_NOW, PSTATE_SIZE, "[ØØ]", 0);

	// recalculate gstate, mosmix, then choose storage strategy potd
	calculate_gstate();
	calculate_mosmix();
	choose_program();

	// print actual gstate
	dump_table((int*) GSTATE_TODAY, GSTATE_SIZE, 24, now->tm_hour, "FRONIUS gstate_hours", GSTATE_HEADER);
	print_gstate(NULL);
}

static void minly(time_t now_ts) {
	// xlog("FRONIUS executing minutely tasks...");

	// aggregate 59 seconds into current minute
	// dump_table((int*) pstate_seconds, PSTATE_SIZE, 60, -1, "FRONIUS pstate_seconds", PSTATE_HEADER);
	aggregate_table((int*) PSTATE_MIN_NOW, (int*) pstate_seconds, PSTATE_SIZE, 60);
	if (pstate->pv)
		dump_struct((int*) PSTATE_MIN_NOW, PSTATE_SIZE, "[ØØ]", 0);

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

	// collect actual power states
#ifndef FRONIUS_MAIN
	for (device_t **dd = DEVICES; *dd; dd++)
		DD->power = tasmota_power_get(DD->id, DD->r);
#endif

	// the FRONIUS main loop
	while (1) {

		// get actual time and make a copy
		now_ts = time(NULL);
		localtime(&now_ts);
		memcpy(now, lt, sizeof(*lt));

		// update state and counter pointers
		counter = COUNTER_NOW;
		gstate = GSTATE_NOW;
		pstate = PSTATE_NOW;

		// wait till this second is over, meanwhile sunspec threads have values updated
		while (now_ts == time(NULL))
			msleep(333);

		// calculate new pstate
		calculate_pstate();

		// initialize program of the day if not yet done and choose storage strategy
		if (!potd) {
			calculate_gstate();
			calculate_mosmix();
			choose_program();
			print_state(0);
			continue;
		}

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

		// prio3: ramp up/down
		if (!device && PSTATE_RAMP)
			device = ramp();

		// prio4: check if higher priorized device can steal from lower priorized
		if (!device)
			device = steal();

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

static int init() {
	set_debug(1);

	// create a socket for sending UDP messages
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == 0)
		return xerr("Error creating socket");

	// initialize hourly & daily & monthly
	time_t now_ts = time(NULL);
	lt = localtime(&now_ts);
	memcpy(now, lt, sizeof(*lt));

	// initialize all devices with start values
	xlog("FRONIUS initializing devices");
	for (device_t **dd = DEVICES; *dd; dd++) {
		DD->state = Active;
		DD->power = -1;
		if (!DD->id)
			DD->addr = resolve_ip(DD->name);
		if (DD->adj && DD->addr == 0)
			DD->state = Disabled; // disable when we don't have an ip address to send UDP messages
	}

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

// run the main loop forever
static int loop() {
	init();
	fronius();
	pause();
	stop();
	return 0;
}

// do all calculations in one single round trip and exit
static int single() {
	time_t now_ts = time(NULL);

	init();

	gstate = GSTATE_NOW;
	counter = COUNTER_NOW;
	pstate = PSTATE_NOW;
	sleep(1); // update values

	calculate_pstate();
	calculate_gstate();
	calculate_mosmix(now_ts);
	choose_program();

	response(&b1);
	standby();
	ramp();
	steal();

	// aggregate 24 pstate hours into one day
	pstate_t pd;
	aggregate_table((int*) &pd, (int*) pstate_hours, PSTATE_SIZE, 24);
	dump_table((int*) pstate_hours, PSTATE_SIZE, 24, now->tm_hour, "FRONIUS pstate_hours", PSTATE_HEADER);
	dump_struct((int*) &pd, PSTATE_SIZE, "[ØØ]", 0);
	print_state(NULL);

	// aggregate 24 gstate hours into one day
	gstate_t gd;
	aggregate_table((int*) &gd, (int*) GSTATE_TODAY, GSTATE_SIZE, 24);
	dump_table((int*) GSTATE_TODAY, GSTATE_SIZE, 24, now->tm_hour, "FRONIUS gstate_hours", GSTATE_HEADER);
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
	for (int i = 0; i < 24 * 7; i++)
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

static int test() {
	potd = (potd_t*) &MODEST;

	printf("\n>>> for forward\n");
	for (device_t **dd = potd->devices; *dd; dd++)
		printf("%s\n", DD->name);

	device_t **dd;

	printf("\n>>> while forward\n");
	dd = potd->devices;
	while (*dd) {
		printf("%s\n", DD->name);
		dd++;
	}

	printf("\n>>> while backward\n");
	while (dd-- != potd->devices)
		printf("%s\n", DD->name);

	printf("\n>>> do forward\n");
	dd = potd->devices;
	do {
		printf("%s\n", DD->name);
		dd++;
	} while (*dd);

	printf("\n<<< do backward\n");
	dd--;
	do {
		printf("%s\n", DD->name);
	} while (dd-- != potd->devices);

	return 0;
}

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

	for (device_t **dd = DEVICES; *dd; dd++) {
		if (!strcmp(DD->name, name)) {
			xlog("FRONIUS Activating Override on %s", DD->name);
			DD->override = time(NULL) + seconds;
			DD->state = Active;
			ramp_device(DD, DD->total);
		}
	}
	return 0;
}

int fronius_override(const char *name) {
	return fronius_override_seconds(name, OVERRIDE);
}

int ramp_heater(device_t *heater, int power) {
	if (!power || heater->state == Disabled || heater->state == Standby)
		return 0; // continue loop

	// keep on as long as we have enough power and device is already on
	if (power > 0 && heater->power)
		return 0; // continue loop

	// check if enough power is available to switch on
	if (power > 0 && !heater->power) {
		int min = rampup_min(heater);
		if (power < min)
			return 0; // continue loop
		xdebug("FRONIUS ramp_heater() %s %d (min %d)", heater->name, power, min);
	}

	// transform power into on/off
	power = power > 0 ? 1 : 0;

	// check if override is active
	power = check_override(heater, power);

	// check if update is necessary
	if (heater->power == power)
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
int ramp_boiler(device_t *boiler, int power) {
	if (!power || boiler->state == Disabled || boiler->state == Standby)
		return 0; // continue loop

	// init
	if (boiler->power == -1)
		return boiler_send(boiler, 0);

	// already full up
	if (boiler->power == 100 && power > 0)
		return 0;

	// already full down
	if (boiler->power == 0 && power < 0)
		return 0;

	// power steps
	int step = power / (boiler->total / 100);
	if (!step)
		return 0;

	// do smaller up steps / bigger down steps when we have distortion
	if (PSTATE_DISTORTION) {
		if (step > 1)
			step /= 2;
		if (step < -1)
			step *= 2;
	}

	// transform power into 0..100%
	power = boiler->power + step;
	if (power < 0)
		power = 0;
	if (power > 100)
		power = 100;

	// check if override is active
	power = check_override(boiler, power);

	// check if update is necessary
	if (boiler->power == power)
		return 0; // continue loop

	// send UDP message to device
	return boiler_send(boiler, power);
}

int ramp_akku(device_t *akku, int power) {
	if (!f10 || !pstate)
		return 0;

	// init
	if (akku->power == -1) {

		// enable discharging
		if (PSTATE_OFFLINE)
			return akku_discharge(akku);

		// set to standby and wait for ramp request
		return akku_standby(akku);
	}

	// ramp down request
	if (power < 0) {

		// skip ramp downs if we still have enough surplus (akku ramps down itself)
		int surp = (pstate->akku - pstate->ramp) * -1;
		if (surp > 0) {
			// xdebug("FRONIUS skipping akku rampdown request surp=%d (akku=%d ramp=%d)", surp, pstate->akku, pstate->ramp);
			return 1; // loop done
		}

		// set to standby as long as other devices active
		if (PSTATE_ACTIVE)
			return akku_standby(akku);

		// skip ramp downs above -50 till pv tendence get's negative
		int tend = tendence();
		if (-NOISE * 2 < pstate->ramp && tend >= 0) {
			xdebug("FRONIUS skipping akku rampdown request %d tend=%d", pstate->ramp, tend);
			return 1; // loop done
		}

		// ramp down - enable discharging
		return akku_discharge(akku);
	}

	// ramp up request
	if (power > 0) {

		// set into standby when full
		if (pstate->soc == 1000)
			return akku_standby(akku);

		// ramp up
		return akku_charge(akku);
	}

	return 0;
}

int fronius_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	// no arguments - run one single roundtrip
	if (argc == 1)
		return single();

	int c;
	while ((c = getopt(argc, argv, "b:c:m:o:fglt")) != -1) {
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
		case 'l':
			return loop();
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
