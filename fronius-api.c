/**
 * !!! Warning !!! deprecated unmaintained and untested code, use fronius-modbus.c
 */
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
#include "frozen.h"
#include "mosmix.h"
#include "utils.h"
#include "curl.h"
#include "mcp.h"

#define URL_READABLE			"http://fronius/components/readable"
#define URL_METER				"http://fronius/solar_api/v1/GetMeterRealtimeData.cgi?Scope=Device&DeviceId=0"
#define URL_FLOW10				"http://fronius10/solar_api/v1/GetPowerFlowRealtimeData.fcgi"
#define URL_FLOW7				"http://fronius7/solar_api/v1/GetPowerFlowRealtimeData.fcgi"

#define COUNTER_HISTORY			30		// days
#define PSTATE_HISTORY			32		// samples

#define MIN_SOC					70
#define AKKU_CAPACITY			11059
#define AKKU_CAPACITY_SOC(soc)	(AKKU_CAPACITY * soc / 1000)
#define EMERGENCY				(AKKU_CAPACITY / 10)

#define WAIT_BURNOUT			1800
#define WAIT_OFFLINE			900
#define WAIT_STANDBY			300
#define WAIT_STABLE				60
#define WAIT_INSTABLE			20
#define WAIT_NEXT				5
#define WAIT_AKKU_RAMP			10
#define WAIT_RAMP				3
#define WAIT_RESPONSE			3		// TODO reicht manchmal nicht?

#define DD						(*dd)

#define JMC						" SMARTMETER_ENERGYACTIVE_CONSUMED_SUM_F64:%f "
#define JMP						" SMARTMETER_ENERGYACTIVE_PRODUCED_SUM_F64:%f "
#define JMV1					" SMARTMETER_VOLTAGE_MEAN_01_F64:%f "
#define JMV2					" SMARTMETER_VOLTAGE_MEAN_02_F64:%f "
#define JMV3					" SMARTMETER_VOLTAGE_MEAN_03_F64:%f "
#define JMF						" SMARTMETER_FREQUENCY_MEAN_F64:%f "

#define JBSOC					" BAT_VALUE_STATE_OF_CHARGE_RELATIVE_U16:%f "

#define JIE1					" PV_ENERGYACTIVE_ACTIVE_SUM_01_U64:%f "
#define JIE2					" PV_ENERGYACTIVE_ACTIVE_SUM_02_U64:%f "
#define JIP						" PV_POWERACTIVE_SUM_F64:%f "

#define JMMPP					" PowerReal_P_Sum:%f "
#define JMMC					" EnergyReal_WAC_Sum_Consumed:%f "
#define JMMP					" EnergyReal_WAC_Sum_Produced:%f "

#define MOSMIX3X24				"FRONIUS mosmix Rad1h/SunD1/RSunD today %d/%d/%d tomorrow %d/%d/%d tomorrow+1 %d/%d/%d"

typedef struct _raw raw_t;

struct _raw {
	float akku;
	float grid;
	float load;
	float pv10;
	float pv10_total1;
	float pv10_total2;
	float pv7;
	float pv7_total;
	float soc;
	float produced;
	float consumed;
	float p;
	float v1;
	float v2;
	float v3;
	float f;
};

// counter history every hour over one day and access pointers
static counter_t counter_hours[24], counter_current, *counter = &counter_current;
#define COUNTER_NOW				(&counter_hours[now->tm_hour])
#define COUNTER_LAST			(&counter_hours[now->tm_hour >  0 ? now->tm_hour - 1 : 23])
#define COUNTER_NEXT			(&counter_hours[now->tm_hour < 23 ? now->tm_hour + 1 :  0])
#define COUNTER_HOUR(h)			(&counter_hours[h])
#define COUNTER_0				(&counter_hours[0])

// 24/7 gstate history slots and access pointers
static gstate_t gstate_hours[24 * 7], gstate_current, *gstate = &gstate_current;
#define GSTATE_NOW				(&gstate_hours[24 * now->tm_wday + now->tm_hour])
#define GSTATE_LAST				(&gstate_hours[24 * now->tm_wday + now->tm_hour - (now->tm_wday == 0 && now->tm_hour ==  0 ?  24 * 7 - 1 : 1)])
#define GSTATE_NEXT				(&gstate_hours[24 * now->tm_wday + now->tm_hour + (now->tm_wday == 6 && now->tm_hour == 23 ? -24 * 7 + 1 : 1)])
#define GSTATE_TODAY			(&gstate_hours[24 * now->tm_wday])
#define GSTATE_YDAY				(&gstate_hours[24 * (now->tm_wday > 0 ? now->tm_wday - 1 : 6)])
#define GSTATE_HOUR(h)			(&gstate_hours[24 * now->tm_wday + (h)])
#define GSTATE_YDAY_HOUR(h)		(&gstate_hours[24 * (now->tm_wday > 0 ? now->tm_wday - 1 : 6) + (h)])
#define GSTATE_D_H(d, h)		(&gstate_hours[24 * (d) + (h)])

// round robin power power flow and state calculations
static pstate_t pstate_history[PSTATE_HISTORY], pstate_current, *pstate = &pstate_current;
static int pstate_history_ptr = 0;

// average loads over 24/7
static int loads[24];

// program of the day - choose by mosmix forecast data
static potd_t *potd = 0;

static struct tm *lt, now_tm, *now = &now_tm;
static int sock = 0;

// reading Inverter API: CURL handles, response memory, raw date, error counter
static CURL *curl10, *curl7, *curl_readable;
static response_t memory = { 0 };
static raw_t raw, *r = &raw;
static int errors;

static device_t* get_by_name(const char *name) {
	for (device_t **dd = DEVICES; *dd; dd++)
		if (!strcmp(DD->name, name))
			return DD;

	return 0;
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

//static int parse_meter(response_t *resp) {
//	int ret = json_scanf(resp->buffer, resp->size, "{ Body { Data { "JMMPP JMMC JMMP" } } }", &r->p, &r->consumed, &r->produced);
//	if (ret != 3)
//		return xerr("FRONIUS parse_meter() warning! parsing Body->Data: expected 3 values but got %d", ret);
//
//	return 0;
//}

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

static pstate_t* get_pstate_history(int offset) {
	int i = pstate_history_ptr + offset;
	if (i < 0)
		i += PSTATE_HISTORY;
	if (i >= PSTATE_HISTORY)
		i -= PSTATE_HISTORY;
	return &pstate_history[i];
}

static void store_meter_power(device_t *d) {
	d->p1 = pstate->p1;
	d->p2 = pstate->p2;
	d->p3 = pstate->p3;
}

static void collect_loads() {
	char line[LINEBUF], value[10];

	ZERO(loads);
	for (int h = 0; h < 24; h++) {
		for (int d = 0; d < 7; d++) {
			int load = GSTATE_D_H(d, h)->load * -1;
			if (load == 0)
				load = BASELOAD;
			if (load < NOISE) {
				xdebug("FRONIUS suspicious collect_loads day=%d hour=%d load=%d --> using BASELOAD", d, h, load);
				load = BASELOAD;
			}
			loads[h] += load;
		}
		loads[h] /= 7;
	}

	strcpy(line, "FRONIUS average 24/7 loads:");
	for (int h = 0; h < 24; h++) {
		snprintf(value, 10, " %d", loads[h]);
		strcat(line, value);
	}
	xdebug(line);

#ifndef FRONIUS_MAIN
	store_array_csv(loads, 24, "  load", LOADS_CSV);
#endif
}

static int collect_heating_total() {
	int total = 0;
	for (device_t **dd = DEVICES; *dd; dd++)
		if (!DD->adj)
			total += DD->total;
	return total;
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

static void print_gstate() {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS");
	xlogl_int_b(line, "∑PV", gstate->pv);
	xlogl_int_noise(line, NOISE, 0, "↑Grid", gstate->produced);
	xlogl_int_noise(line, NOISE, 1, "↓Grid", gstate->consumed);
	xlogl_int(line, "Today", gstate->today);
	xlogl_int(line, "Tomo", gstate->tomorrow);
	xlogl_int(line, "SoD", gstate->sod);
	xlogl_int(line, "EoD", gstate->eod);
	xlogl_int(line, "Akku", gstate->akku);
	xlogl_float(line, "SoC", FLOAT10(gstate->soc));
	xlogl_float(line, "TTL", FLOAT60(gstate->ttl));
	xlogl_bits16(line, "  flags", gstate->flags);
	strcat(line, " ");
	xlogl_float_noise(line, 0.1, 0, "Success", FLOAT100(gstate->success));
	xlogl_float_noise(line, 0.0, 0, "Survive", FLOAT100(gstate->survive));
	xlogl_float_noise(line, 0.0, 0, "Heating", FLOAT100(gstate->heating));
	strcat(line, "  potd:");
	strcat(line, potd ? potd->name : "NULL");
	xlogl_end(line, strlen(line), 0);
}

static void print_pstate_dstate(device_t *d) {
	char line[512], value[16]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS  ");

	for (device_t **dd = potd->devices; *dd; dd++) {
		switch (DD->state) {
		case Disabled:
			snprintf(value, 5, " .");
			break;
		case Active:
		case Active_Checked:
			if (DD->adj)
				snprintf(value, 5, " %3d", DD->power);
			else
				snprintf(value, 5, " %c", DD->power ? 'X' : '_');
			break;
		case Standby:
			snprintf(value, 5, " S");
			break;
		case Standby_Check:
			snprintf(value, 5, " s");
			break;
		case Charge:
			snprintf(value, 5, " C");
			break;
		case Discharge:
			snprintf(value, 5, " D");
			break;
		default:
			snprintf(value, 5, " ?");
			break;
		}
		strcat(line, value);
	}

	strcat(line, "   nr ");
	for (device_t **dd = potd->devices; *dd; dd++) {
		snprintf(value, 5, "%d", DD->noresponse);
		strcat(line, value);
	}

	xlogl_bits16(line, "  flags", pstate->flags);
	xlogl_int_b(line, "PV10", pstate->mppt1 + pstate->mppt2);
	xlogl_int_b(line, "PV7", pstate->mppt3 + pstate->mppt4);
	xlogl_int_noise(line, NOISE, 1, "Grid", pstate->grid);
	xlogl_int_noise(line, NOISE, 1, "Akku", pstate->akku);
	xlogl_int_noise(line, NOISE, 0, "Ramp", pstate->ramp);
	xlogl_int(line, "Load", pstate->load);
	xlogl_end(line, strlen(line), 0);
}

// call device specific ramp function
static int ramp(device_t *d, int power) {
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

	// potd has changed - reset all devices and wait for new values
	for (device_t **dd = DEVICES; *dd; dd++)
		ramp(DD, DOWN);

	xlog("FRONIUS selecting %s program of the day", p->name);
	potd = (potd_t*) p;

	return 0;
}

// choose program of the day
static int choose_program() {
	// return select_program(&GREEDY);
	// return select_program(&MODEST);

	// summer
	if (SUMMER)
		return select_program(&PLENTY);

	// akku is empty - charging akku has priority
	if (gstate->soc < 100)
		return select_program(&MODEST);

	// we will NOT survive - charging akku has priority
	if (gstate->survive < 0)
		return select_program(&MODEST);

	// survive but tomorrow not enough PV - charging akku has priority
	if (WINTER && gstate->tomorrow < AKKU_CAPACITY)
		return select_program(&MODEST);

	// quota not yet reached and akku not yet enough to survive
	if (gstate->success < 0 && gstate->akku < gstate->need_survive)
		return select_program(&MODEST);

	// start heating asap and charge akku tommorrow
	if (gstate->tomorrow > gstate->today)
		return select_program(&GREEDY);

	// survive but not enough for heating --> load boilers
	if (gstate->heating <= 0)
		return select_program(&BOILERS);

	// enough PV available to survive + heating
	return select_program(&PLENTY);
}

static void emergency() {
	for (device_t **dd = DEVICES; *dd; dd++)
		ramp(DD, DOWN);
	xlog("FRONIUS emergency shutdown at akku=%d grid=%d ", pstate->akku, pstate->grid);
}

static void burnout() {
	fronius_override_seconds("küche", WAIT_BURNOUT);
	fronius_override_seconds("wozi", WAIT_BURNOUT);
	xlog("FRONIUS burnout soc=%.1f temp=%.1f", FLOAT10(gstate->soc), TEMP_IN);
}

static int ramp_multi(device_t *d) {
	int ret = ramp(d, pstate->ramp);
	if (ret) {
		// recalculate ramp power
		int old_ramp = pstate->ramp;
		pstate->ramp -= d->delta + d->delta / 2; // add 50%
		// too less to forward
		if (old_ramp > 0 && pstate->ramp < NOISE * 2)
			pstate->ramp = 0;
		if (old_ramp < 0 && pstate->ramp > NOISE * -2)
			pstate->ramp = 0;
		if (pstate->ramp)
			msleep(33);
	}
	return ret;
}

static device_t* rampup() {
	if (PSTATE_ALL_UP || PSTATE_ALL_STANDBY)
		return 0;

	if (!PSTATE_STABLE || !PSTATE_VALID || PSTATE_DISTORTION)
		if (pstate->ramp < 1000)
			return 0;

	device_t *d = 0, **dd = potd->devices;
	while (*dd && pstate->ramp > 0) {
		if (ramp_multi(DD))
			d = DD;
		dd++;
	}

	return d;
}

static device_t* rampdown() {
	if (PSTATE_ALL_DOWN || PSTATE_ALL_STANDBY)
		return 0;

	// jump to last entry
	device_t *d = 0, **dd = potd->devices;
	while (*dd)
		dd++;

	// now go backward - this gives reverse order
	while (dd-- != potd->devices && pstate->ramp < 0)
		if (ramp_multi(DD))
			d = DD;

	return d;
}

static device_t* steal() {
	if (PSTATE_ALL_UP || PSTATE_ALL_DOWN || PSTATE_ALL_STANDBY || !PSTATE_STABLE || !PSTATE_VALID || PSTATE_DISTORTION)
		return 0;

	for (device_t **dd = potd->devices; *dd; dd++) {
		// thief not active or in standby - TODO akku cannot actively steal - only by ramping down victims
		if (DD == AKKU || DD->state == Disabled || DD->state == Standby)
			continue;

		// thief already (full) on
		if (DD->power == (DD->adj ? 100 : 1))
			continue;

		// thief can steal akkus charge power or victims load when not in override mode and zero noresponse counter
		int p = 0;
		for (device_t **vv = dd + 1; *vv; vv++)
			if (*vv == AKKU)
				p += pstate->akku < -MINIMUM ? pstate->akku * -0.9 : 0;
			else
				p += !(*vv)->override && !(*vv)->noresponse ? (*vv)->load : 0;

		// adjustable: 1% of total, dumb: total
		int min = DD->adj ? DD->total / 100 : DD->total;
		min += min / 10; // add 10%

		// not enough to steal
		if (p < min)
			continue;

		// ramp up thief - victims gets automatically ramped down in next round
		if (ramp(DD, p)) {
			xdebug("FRONIUS %s steal %d (min=%d)", DD->name, p, min);
			return DD;
		}
	}

	return 0;
}

static device_t* perform_standby(device_t *d) {
	int power = d->adj ? (d->power < 50 ? +500 : -500) : (d->power ? d->total * -1 : d->total);
	xdebug("FRONIUS starting standby check on %s with power=%d", d->name, power);
	d->state = Standby_Check;
	ramp(d, power);
	return d;
}

static device_t* standby() {
	if (PSTATE_ALL_STANDBY || !PSTATE_CHECK_STANDBY || !PSTATE_STABLE || !PSTATE_VALID || PSTATE_DISTORTION || pstate->pv < BASELOAD * 2)
		return 0;

	// try first active powered adjustable device with noresponse counter > 0
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD->state == Active && DD->power && DD->adj && DD->noresponse > 0)
			return perform_standby(DD);

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
	// akku or no expected delta load - no response to check
	if (d == AKKU || !d->delta)
		return 0;

	// valid response is at least 2/3 of last ramp
	int delta = d->delta - d->delta / 3;
	d->delta = 0; // reset

	// calculate delta power per phase
	int d1 = pstate->p1 - d->p1;
	if (-NOISE < d1 && d1 < NOISE)
		d1 = 0;
	int d2 = pstate->p2 - d->p2;
	if (-NOISE < d2 && d2 < NOISE)
		d2 = 0;
	int d3 = pstate->p3 - d->p3;
	if (-NOISE < d3 && d3 < NOISE)
		d3 = 0;

	// check if we got a response on any phase
	int r1 = delta > 0 ? d1 > delta : d1 < delta;
	int r2 = delta > 0 ? d2 > delta : d2 < delta;
	int r3 = delta > 0 ? d3 > delta : d3 < delta;
	int r = r1 || r2 || r3;

	// response OK
	if (r && (d->state == Active || d->state == Active_Checked)) {
		xdebug("FRONIUS response OK from %s, delta expected %d actual %d %d %d", d->name, delta, d1, d2, d3);
		d->noresponse = 0;
		return 0;
	}

	// standby check was negative - we got a response
	if (d->state == Standby_Check && r) {
		xdebug("FRONIUS standby check negative for %s, delta expected %d actual %d %d %d", d->name, delta, d1, d2, d3);
		d->noresponse = 0;
		d->state = Active_Checked; // mark Active with standby check performed
		return d; // recalculate in next round
	}

	// standby check was positive -> set device into standby
	if (d->state == Standby_Check && !r) {
		xdebug("FRONIUS standby check positive for %s, delta expected %d actual %d %d %d  --> entering standby", d->name, delta, d1, d2, d3);
		ramp(d, d->total * -1);
		d->noresponse = d->delta; // no response from switch off expected
		d->state = Standby;
		return d; // recalculate in next round
	}

	// ignore standby check when power was released
	if (delta < 0)
		return 0;

	// perform standby check when noresponse counter reaches threshold
	if (++d->noresponse >= STANDBY_NORESPONSE)
		return perform_standby(d);

	xdebug("FRONIUS no response from %s count %d/%d", d->name, d->noresponse, STANDBY_NORESPONSE);
	return 0;
}

static void calculate_gstate() {
	// clear state flags and values
	gstate->flags = 0;

	// take over SoC
	gstate->soc = pstate->soc;

	// average load
	gstate->load += pstate->load;
	gstate->load /= 2;

	// day total: pv / consumed / produced
	counter_t *c = COUNTER_0;
	gstate->pv = 0;
	gstate->pv += counter->mppt1 && c->mppt1 ? counter->mppt1 - c->mppt1 : 0;
	gstate->pv += counter->mppt2 && c->mppt2 ? counter->mppt2 - c->mppt2 : 0;
	gstate->pv += counter->mppt3 && c->mppt3 ? counter->mppt3 - c->mppt3 : 0;
	gstate->pv += counter->mppt4 && c->mppt4 ? counter->mppt4 - c->mppt4 : 0;
	gstate->produced = counter->produced && c->produced ? counter->produced - c->produced : 0;
	gstate->consumed = counter->consumed && c->consumed ? counter->consumed - c->consumed : 0;

	// akku usable energy and estimated time to live based on last hour's average load +5% extra +50Wh inverter dissipation
	gstate->akku = gstate->soc > MIN_SOC ? AKKU_CAPACITY_SOC(gstate->soc - MIN_SOC) : 0;
	gstate->ttl = gstate->soc > MIN_SOC ? gstate->akku * 60 / (gstate->load + gstate->load / 20 - 50) * -1 : 0;

	// collect mosmix forecasts
	int today, tomorrow, sod, eod;
	mosmix_collect(now, &today, &tomorrow, &sod, &eod);
	gstate->today = today;
	gstate->tomorrow = tomorrow;
	gstate->sod = sod;
	gstate->eod = eod;

	// success factor
	float success = gstate->sod ? (float) gstate->pv / (float) gstate->sod - 1.0 : 0;
	gstate->success = success * 100; // store as x100 scaled
	if (gstate->success > 1000)
		gstate->success = 1000;
	xdebug("FRONIUS success sod=%d pv=%d --> %.2f", gstate->sod, gstate->pv, success);

	// collect needed power to survive (+50Wh inverter dissipation) and to heat
	int heating_total = collect_heating_total();
	gstate->need_survive = mosmix_survive(now, loads, BASELOAD, 50);
	gstate->need_heating = mosmix_heating(now, heating_total);

	// survival factor
	int tocharge = gstate->need_survive - gstate->akku;
	if (tocharge < 0)
		tocharge = 0;
	int available = gstate->eod - tocharge;
	if (available < 0)
		available = 0;
	float survive = gstate->need_survive ? (float) (gstate->akku + available) / (float) gstate->need_survive - 1.0 : 0;
	gstate->survive = survive * 100; // store as x100 scaled
	if (gstate->survive > 1000)
		gstate->survive = 1000;
	xdebug("FRONIUS survive eod=%d tocharge=%d available=%d akku=%d needed=%d --> %.2f", gstate->eod, tocharge, available, gstate->akku, gstate->need_survive, survive);

	// heating factor
	float heating = gstate->need_heating ? (float) available / (float) gstate->need_heating - 1.0 : 0;
	gstate->heating = heating * 100; // store as x100 scaled
	if (gstate->heating > 1000)
		gstate->heating = 1000;
	xdebug("FRONIUS heating eod=%d tocharge=%d available=%d needed=%d --> %.2f", gstate->eod, tocharge, available, gstate->need_heating, heating);

	// heating enabled
	gstate->flags |= FLAG_HEATING;
	// no need to heat
	if (TEMP_IN > 25)
		gstate->flags &= !FLAG_HEATING;
	if (TEMP_IN > 18 && SUMMER)
		gstate->flags &= !FLAG_HEATING;
	if (TEMP_IN > 20 && TEMP_OUT > 15 && !SUMMER)
		gstate->flags &= !FLAG_HEATING;
	// force heating independently from temperature
	if ((now->tm_mon == 4 || now->tm_mon == 8) && now->tm_hour >= 16) // may/sept begin 16 o'clock
		gstate->flags |= FLAG_HEATING;
	else if ((now->tm_mon == 3 || now->tm_mon == 9) && now->tm_hour >= 14) // apr/oct begin 14 o'clock
		gstate->flags |= FLAG_HEATING;
	else if ((now->tm_mon < 3 || now->tm_mon > 9) && TEMP_IN < 28) // nov-mar always if not too hot
		gstate->flags |= FLAG_HEATING;
	if (GSTATE_HEATING)
		xdebug("FRONIUS heating enabled month=%d temp_in=%d temp_ou=%d", now->tm_mon, TEMP_IN, TEMP_OUT);
}

static void calculate_pstate1() {
	// clear state flags and values
	pstate->flags = pstate->ramp = 0;

	// take over raw values from Fronius10
	// TODO separate mppt1 + mppt2, dc10, ac10
	pstate->akku = r->akku;
	pstate->grid = r->grid;
	pstate->load = r->load;
	pstate->mppt1 = r->pv10;
	pstate->soc = r->soc * 10.0;

	// get 2x history back
	pstate_t *h1 = get_pstate_history(-1);
	pstate_t *h2 = get_pstate_history(-2);

	// grid, delta grid and sum
	pstate->dgrid = pstate->grid - h1->grid;
	if (abs(pstate->dgrid) < NOISE)
		pstate->dgrid = 0; // shape dgrid

	// load, delta load + sum
	pstate->load = (pstate->ac10 + pstate->ac7 + pstate->grid) * -1;
	pstate->dload = pstate->load - h1->load;
	if (abs(pstate->dload) < NOISE)
		pstate->dload = 0; // shape dload

	// check if we have delta ac power anywhere
	if (abs(pstate->grid - h1->grid) > NOISE)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac10 - h1->ac10) > NOISE)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac7 - h1->ac7) > NOISE)
		pstate->flags |= FLAG_DELTA;

	// offline mode when 3x not enough PV production
	int online = pstate->pv > MINIMUM || h1->pv > MINIMUM || h2->pv > MINIMUM;
	if (!online) {
		// akku burn out between 6 and 9 o'clock if we can re-charge it completely by day
		int burnout_time = now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8;
		int burnout_possible = TEMP_IN < 18 && pstate->soc > 150;
		if (burnout_time && burnout_possible && AKKU_BURNOUT)
			pstate->flags |= FLAG_BURNOUT; // burnout
		else
			pstate->flags |= FLAG_OFFLINE; // offline
		pstate->ramp = pstate->xload = pstate->dxload = pstate->pv = pstate->dpv = pstate->mppt1 = pstate->mppt2 = pstate->mppt3 = pstate->mppt4 = 0;
		return;
	}

	// emergency shutdown when three times extreme akku discharge or grid download
	if (pstate->akku > EMERGENCY && h1->akku > EMERGENCY && h2->akku > EMERGENCY && pstate->grid > EMERGENCY && h1->grid > EMERGENCY && h2->grid > EMERGENCY) {
		pstate->flags |= FLAG_EMERGENCY;
		return;
	}

	pstate->flags |= FLAG_VALID;
}

static void calculate_pstate2() {
	// take over raw values from Fronius7
	// TODO separate mppt3 + mppt7, dc7, ac7
	pstate->mppt3 = r->pv7;

	// clear VALID flag
	pstate->flags &= ~FLAG_VALID;

	// get 2x history back
	pstate_t *h1 = get_pstate_history(-1);
	pstate_t *h2 = get_pstate_history(-2);

	// total pv from both inverters
	pstate->pv = pstate->mppt1 + pstate->mppt2 + pstate->mppt3 + pstate->mppt4;
	pstate->dpv = pstate->pv - h1->pv;

	// calculate ramp up/down power
	pstate->ramp = (pstate->grid + pstate->akku) * -1;
	if (-RAMP_WINDOW < pstate->grid && pstate->grid < 0)
		pstate->ramp = 0; 									// stable window between 0..25
	if (0 < pstate->grid && pstate->grid < RAMP_WINDOW)
		pstate->ramp = -RAMP_WINDOW; 						// ramp down as soon as we are consuming grid
	if (pstate->akku < -NOISE && -RAMP_WINDOW < pstate->grid && pstate->grid < RAMP_WINDOW)
		pstate->ramp = 0; 									// akku is regulating around 0 so set stable window between -25..+25

	// state is stable when we have three times no grid changes
	if (!pstate->dgrid && !h1->dgrid && !h2->dgrid)
		pstate->flags |= FLAG_STABLE;

	// distortion when delta pv is too big
	int dpv_sum = 0;
	for (int i = 0; i < PSTATE_HISTORY; i++)
		dpv_sum += abs(pstate_history[i].dpv);
	if (dpv_sum > 1000)
		pstate->flags |= FLAG_DISTORTION;
	if (PSTATE_DISTORTION)
		xdebug("FRONIUS distortion=%d sum=%d", PSTATE_DISTORTION, dpv_sum);

	// device loop:
	// - xload/dxload
	// - all devices up/down/standby
	pstate->xload = pstate->dxload = 0;
	pstate->flags |= FLAG_ALL_UP | FLAG_ALL_DOWN | FLAG_ALL_STANDBY;
	for (device_t **dd = DEVICES; *dd; dd++) {
		pstate->xload += DD->load;
		// (!) power can be -1 when uninitialized
		if (DD->power > 0)
			pstate->flags &= ~FLAG_ALL_DOWN;
		if (!DD->power || (DD->adj && DD->power != 100))
			pstate->flags &= ~FLAG_ALL_UP;
		if (DD->state != Standby)
			pstate->flags &= ~FLAG_ALL_STANDBY;
	}
	int p_load = -1 * pstate->load; // - BASELOAD;
	pstate->dxload = p_load > 0 && pstate->xload ? p_load * 100 / pstate->xload : 0;

	// indicate standby check when deviation between actual load and calculated load is three times above 33%
	if (pstate->dxload > 33 && h1->dxload > 33 && h2->dxload > 33)
		pstate->flags |= FLAG_CHECK_STANDBY;
	if (PSTATE_CHECK_STANDBY)
		xdebug("FRONIUS set FLAG_CHECK_STANDBY load=%d xload=%d dxload=%d", pstate->load, pstate->xload, pstate->dxload);

	// clear flag when values not valid
	pstate->flags |= FLAG_VALID;
	int sum = pstate->grid + pstate->akku + pstate->load + pstate->mppt1 + pstate->mppt2;
	if (abs(sum) > SUSPICIOUS) {
		xdebug("FRONIUS suspicious values detected: sum=%d", sum); // probably inverter power dissipations (?)
		pstate->flags &= ~FLAG_VALID;
	}
	if (pstate->load > 0) {
		xdebug("FRONIUS positive load detected");
		pstate->flags &= ~FLAG_VALID;
	}
	if (pstate->grid < -NOISE && pstate->akku > NOISE) {
		int waste = abs(pstate->grid) < pstate->akku ? abs(pstate->grid) : pstate->akku;
		xdebug("FRONIUS wasting %d akku -> grid power", waste);
		pstate->flags &= ~FLAG_VALID;
	}
	if (pstate->dgrid > BASELOAD * 2) { // e.g. refrigerator starts !!!
		xdebug("FRONIUS grid spike detected %d: %d -> %d", pstate->grid - h1->grid, h1->grid, pstate->grid);
		pstate->flags &= ~FLAG_VALID;
	}
}

static int calculate_next_round(device_t *d) {
	if (d)
		return WAIT_RESPONSE;

	if (PSTATE_OFFLINE || PSTATE_BURNOUT)
		// return WAIT_OFFLINE;
		return 10;

	// much faster next round on
	// - suspicious values detected
	// - distortion
	// - pv tendence up/down
	// - wasting akku->grid power
	// - big akku discharge or grid download
	// - actual load > calculated load --> other consumers active
	if (!PSTATE_VALID || PSTATE_DISTORTION || pstate->grid > 500 || pstate->akku > 500 || pstate->dxload < -5)
		return WAIT_NEXT;

	if (PSTATE_ALL_STANDBY)
		return WAIT_STANDBY;

	if (PSTATE_STABLE)
		return WAIT_STABLE;

	return WAIT_INSTABLE;
}

static void daily() {
	PROFILING_START
	xlog("FRONIUS executing daily tasks...");

	// recalculate average 24/7 loads
	collect_loads();

	// calculate forecast errors - actual vs. expected
	int forecast_yesterday = GSTATE_YDAY_HOUR(23)->tomorrow;
	float eyesterday = forecast_yesterday ? (float) gstate->pv / (float) forecast_yesterday : 0;
	xdebug("FRONIUS yesterdays forecast for today %d, actual %d, error %.2f", forecast_yesterday, gstate->pv, eyesterday);
	int forecast_today = GSTATE_HOUR(6)->today;
	float etoday = forecast_today ? (float) gstate->pv / (float) forecast_today : 0;
	xdebug("FRONIUS today's 04:00 forecast for today %d, actual %d, error %.2f", forecast_today, gstate->pv, etoday);

	// recalculate mosmix factors
	mosmix_factors();

#ifndef FRONIUS_MAIN
	store_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));
	store_blob(COUNTER_FILE, counter_hours, sizeof(counter_hours));
#endif

	// recalculate gstate
	calculate_gstate();
	print_gstate();

	PROFILING_LOG("FRONIUS daily");
}

static void hourly() {
	PROFILING_START
	xlog("FRONIUS executing hourly tasks...");

	// copy gstate and counters to history
	memcpy(COUNTER_NOW, (void*) counter, sizeof(counter_t));
	memcpy(GSTATE_NOW, (void*) gstate, sizeof(gstate_t));

	// reset average load
	gstate->load = 0;

	// resetting noresponse counters and set all devices back to active
	for (device_t **dd = DEVICES; *dd; dd++) {
		DD->noresponse = 0;
		if (DD->state == Standby || DD->state == Active_Checked)
			if (DD != AKKU)
				DD->state = Active;
	}

	// force all devices off when offline
	if (PSTATE_OFFLINE)
		for (device_t **dd = DEVICES; *dd; dd++)
			ramp(DD, DOWN);

	// calculate last hour produced PV per mppt
	counter_t *c = COUNTER_LAST;
	int mppt1 = counter->mppt1 && c->mppt1 ? counter->mppt1 - c->mppt1 : 0;
	int mppt2 = counter->mppt2 && c->mppt2 ? counter->mppt2 - c->mppt2 : 0;
	int mppt3 = counter->mppt3 && c->mppt3 ? counter->mppt3 - c->mppt3 : 0;
	int mppt4 = counter->mppt4 && c->mppt4 ? counter->mppt4 - c->mppt4 : 0;

	// noise
	if (mppt1 < NOISE)
		mppt1 = 0;
	if (mppt2 < NOISE)
		mppt2 = 0;
	if (mppt3 < NOISE)
		mppt3 = 0;
	if (mppt4 < NOISE)
		mppt4 = 0;

	// reload and update mosmix history, clear at midnight
	mosmix_load(now, MARIENBERG, now->tm_hour == 0);
	mosmix_mppt(now, mppt1, mppt2, mppt3, mppt4);

#ifndef FRONIUS_MAIN
	// we need the history in mosmix.c main()
	mosmix_store_history();
#endif

	PROFILING_LOG("FRONIUS hourly");
}

static void fronius() {
	int hour, day, wait;
	device_t *device = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// get actual time and make a copy
	time_t now_ts = time(NULL);
	localtime(&now_ts);
	memcpy(now, lt, sizeof(*lt));
	day = now->tm_wday;
	hour = now->tm_hour;

	errors = 0;
	wait = 1;

	// once upon start: calculate global state + discharge rate and choose program of the day
	curl_perform(curl_readable, &memory, &parse_readable);
	calculate_gstate();
	choose_program();

	// the FRONIUS main loop
	while (1) {

		sleep(1);
		if (wait--)
			continue;

		// get actual time and make a copy
		now_ts = time(NULL);
		localtime(&now_ts);
		memcpy(now, lt, sizeof(*lt));

		// hourly tasks - before updating counter/gstate
		if (hour != now->tm_hour) {
			hour = now->tm_hour;
			hourly(now_ts);
		}

		// daily tasks - before updating counter/gstate
		if (day != now->tm_wday) {
			day = now->tm_wday;
			daily(now_ts);
		}

		// check error counter
		if (errors > 10)
			for (device_t **dd = DEVICES; *dd; dd++)
				ramp(DD, DOWN);

		// make Fronius10 API call and calculate first pstate
		errors += curl_perform(curl10, &memory, &parse_fronius10);
		calculate_pstate1();

		// check emergency
		if (PSTATE_EMERGENCY)
			emergency();

		// check burnout
		if (PSTATE_BURNOUT)
			burnout();

		if (PSTATE_VALID) {
			// make Fronius7 API call and calculate second pstate
			errors += curl_perform(curl7, &memory, &parse_fronius7);
			calculate_pstate2();
		}

		// prio1: check response from previous action
		if (device)
			device = response(device);

		// prio2: perform standby check logic
		if (!device)
			device = standby();

		// prio3: ramp down in reverse order
		if (!device && pstate->ramp < 0)
			device = rampdown();

		// prio4: ramp up in order
		if (!device && pstate->ramp > 0)
			device = rampup();

		// prio5: check if higher prioritized device can steal from lower prioritized
		if (!device)
			device = steal();

		// print combined device and pstate when we had delta or device action
		if (PSTATE_DELTA || device)
			print_pstate_dstate(device);

		// determine wait for next round
		wait = calculate_next_round(device);

		// print pstate history and device state when we have active devices
		if (!PSTATE_ALL_DOWN)
			dump_table((int*) pstate_history, PSTATE_SIZE, 3, -1, "FRONIUS pstate history", PSTATE_HEADER);

		// update pstate history and calculate new pointer
		pstate_t *h = &pstate_history[pstate_history_ptr];
		memcpy(h, (void*) pstate, sizeof(pstate_t));
		if (++pstate_history_ptr == PSTATE_HISTORY)
			pstate_history_ptr = 0;

		errors = 0;
	}
}

static int init() {
	set_debug(1);

	// create a socket for sending UDP messages
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == 0)
		return xerr("Error creating socket");

	// initialize time structure
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

	ZERO(pstate_history);
	ZERO(gstate_hours);
	ZERO(counter_hours);
	ZEROP(r);

	load_blob(COUNTER_FILE, counter_hours, sizeof(counter_hours));
	load_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));

	mosmix_load_history(now);
	mosmix_factors();
	mosmix_load(now, MARIENBERG, 0);

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
#ifndef FRONIUS_MAIN
	store_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));
	store_blob(COUNTER_FILE, counter_hours, sizeof(counter_hours));
	mosmix_store_history();
#endif

	if (sock)
		close(sock);
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

int fronius_boiler1() {
	return select_program(&BOILER1);
}

int fronius_boiler3() {
	return select_program(&BOILER3);
}

int fronius_override_seconds(const char *name, int seconds) {
	device_t *d = get_by_name(name);
	if (!d)
		return 0;
	if (d->override)
		return 0;

	xlog("FRONIUS Activating Override on %s", d->name);
#ifdef FRONIUS_MAIN
	d->power = -1;
	if (!d->id)
		d->addr = resolve_ip(d->name);
	if (d->adj && d->addr == 0)
		d->state = Disabled; // disable when we don't have an ip address to send UDP messages
#endif
	d->state = Active;
	d->override = time(NULL) + seconds;
	ramp(d, d->total);

	return 0;
}

int fronius_override(const char *name) {
	return fronius_override_seconds(name, OVERRIDE);
}

int ramp_heater(device_t *heater, int power) {
	if (!power || heater->state == Disabled || heater->state == Standby)
		return 0; // continue loop

	// heating disabled except override
	if (power > 0 && !GSTATE_HEATING && !heater->override) {
		power = 0;
		heater->state = Standby;
	}

	// keep on when already on
	if (power > 0 && heater->power)
		return 0; // continue loop

	// not enough power available to switch on
	if (power > 0 && power < heater->total)
		return 0; // continue loop

	// transform power into on/off
	power = power > 0 ? 1 : 0;

	// check if override is active
	power = check_override(heater, power);

	// check if update is necessary
	if (heater->power == power)
		return 0;

	if (power)
		xdebug("FRONIUS switching %s ON", heater->name);
	else
		xdebug("FRONIUS switching %s OFF", heater->name);

#ifndef FRONIUS_MAIN
	int ret = tasmota_power(heater->id, heater->r, power ? 1 : 0);
	if (ret < 0)
		return ret;
#endif

	// update power values
	heater->delta = power ? heater->total : heater->total * -1;
	heater->load = power ? heater->total : 0;
	heater->power = power;
	store_meter_power(heater);
	return WAIT_RESPONSE; // loop done
}

// echo p:0:0 | socat - udp:boiler3:1975
// for i in `seq 1 10`; do let j=$i*10; echo p:$j:0 | socat - udp:boiler1:1975; sleep 1; done
int ramp_boiler(device_t *boiler, int power) {
	if (!power || boiler->state == Disabled || boiler->state == Standby)
		return 0; // continue loop

	// cannot send UDP if we don't have an IP
	if (boiler->addr == NULL)
		return 0;

	// already full up
	if (boiler->power == 100 && power > 0)
		return 0;

	// already full down
	if (boiler->power == 0 && power < 0)
		return 0;

	// power steps
	int step = power * 100 / boiler->total;
	if (!step)
		return 0;

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
	char message[16];
	snprintf(message, 16, "p:%d:%d", power, 0);

	if (step < 0)
		xdebug("FRONIUS ramp↓ %s step %d UDP %s", boiler->name, step, message);
	else
		xdebug("FRONIUS ramp↑ %s step +%d UDP %s", boiler->name, step, message);

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

	// update power values
	boiler->delta = (power - boiler->power) * boiler->total / 100;
	boiler->load = power * boiler->total / 100;
	boiler->power = power;
	store_meter_power(boiler);
	return WAIT_RESPONSE; // loop done
}

int ramp_akku(device_t *akku, int power) {

	// disable response/standby logic
	akku->load = akku->delta = 0;
	akku->power = 1; // always enabled

	// TODO untested logic

	if (power) {

		// allow akku charging by recalculate ramp power only from grid
		pstate->ramp = (pstate->grid) * -1;
		if (PSTATE_ALL_DOWN && pstate->ramp < 0)
			pstate->ramp = 0; // no active devices - nothing to ramp down
		if (0 < pstate->ramp && pstate->ramp < RAMP_WINDOW) // stable between 0..25
			pstate->ramp = 0;

	} else {

		// force akku to release charging power by recalculate ramp power from grid and akku
		pstate->ramp = (pstate->grid + pstate->akku) * -1;
		if (PSTATE_ALL_DOWN && pstate->ramp < 0)
			pstate->ramp = 0; // no active devices - nothing to ramp down
		if (RAMP_WINDOW < pstate->ramp && pstate->ramp < RAMP_WINDOW * 2) // stable between 25..50
			pstate->ramp = 0;

	}

	return 0; // always continue loop to handle new ramp power at next device in chain
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
	while ((c = getopt(argc, argv, "c:o:t")) != -1) {
		printf("getopt %c\n", c);
		switch (c) {
		case 'o':
			return fronius_override(optarg);
		case 't':
			test();
			printf("test\n");
			return 0;
		default:
			xlog("unknown getopt %c", c);
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
