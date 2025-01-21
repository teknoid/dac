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

// program of the day - choose by mosmix forecast data
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
#define GSTATE_NEXT				(&gstate_hours[now->tm_hour < 23 ? now->tm_hour + 1 : 00])
#define GSTATE_HOUR(h)			(&gstate_hours[24 * now->tm_wday + h])
#define GSTATE_TODAY			GSTATE_HOUR(0)

// round robin power power flow and state calculations
static pstate_t pstate_history[PSTATE_HISTORY], *pstate = &pstate_history[0];
static int pstate_history_ptr = 0;

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

static void bump_pstate() {
	// calculate new pstate pointer
	if (++pstate_history_ptr == PSTATE_HISTORY)
		pstate_history_ptr = 0;
	pstate_t *pstate_new = &pstate_history[pstate_history_ptr];

	// take over all values
	memcpy(pstate_new, (void*) pstate, sizeof(pstate_t));
	pstate = pstate_new; // atomic update current pstate pointer
}

// sum up load for darkness hours - take akku discharge values from yesterday
static int collect_load(int from, int hours) {
	int load = 0;
	char line[LINEBUF], value[25];
	strcpy(line, "FRONIUS mosmix load");
	for (int i = from; i < hours; i++) {
		int hour = from + i;
		if (hour >= 24)
			hour -= 24;
		int hload = gstate_hours[hour].dakku;
		load += hload;
		snprintf(value, 25, " %d:%d", hour, hload);
		strcat(line, value);
	}
	xdebug(line);

	// adding +10% Dissipation / Reserve
	load += load / 10;
	return load;
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

	strcat(line, "   st ");
	for (device_t **dd = potd->devices; *dd; dd++) {
		snprintf(value, 5, "%d", DD->state);
		strcat(line, value);
	}

	strcat(line, "   nr ");
	for (device_t **dd = potd->devices; *dd; dd++) {
		snprintf(value, 5, "%d", DD->noresponse);
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

	// potd has changed - reset all devices and wait for new values
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

	// prio2: ramp up in order when stable and ramp indicated
	if (pstate->ramp > 0 && PSTATE_VALID && (PSTATE_STABLE || pstate->ramp > 1000)) {
		d = rampup(pstate->ramp);
		if (d)
			return d;
	}

	return 0;
}

static int steal_thief_victim(device_t *t, device_t *v) {
	// thief not active or in standby
	if (t->state == Disabled || t->state == Standby)
		return 0;

	// thief already (full) on
	if (t->power == (t->adj ? 100 : 1))
		return 0;

	// we can steal akkus charge charge power or victims load
	int steal = 0;
	if (v == AKKU)
		steal = pstate->akku < -100 ? pstate->akku * -0.9 : 0;
	else
		steal = v->load;

	// nothing to steal
	if (!steal)
		return 0;

	// not enough to steal
	int min = t->adj ? t->total / 100 : t->total; // adjustable: 1% of total, dumb: total
	int power = pstate->ramp + steal;
	if (power < min)
		return 0;

	xdebug("FRONIUS steal %d from %s and provide it to %s with a load of %d min=%d", steal, v->name, t->name, t->total, min);

	// ramp down victim, akku ramps down itself
	if (v != AKKU)
		ramp_device(v, v->load * -1);
	ramp_device(t, power);

	// give akku time to adjust
	if (v == AKKU)
		t->timer = WAIT_AKKU_RAMP;

	// no response expected as we put power from one to another device
	t->xload = 0;
	return 1;
}

static device_t* steal() {
	// check flags
	if (!PSTATE_STABLE || !PSTATE_VALID || PSTATE_DISTORTION)
		return 0;

	// jump to end
	device_t **tail = potd->devices;
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
	int power = d->adj ? (d->power < 50 ? +500 : -500) : (d->power ? d->total * -1 : d->total);
	xdebug("FRONIUS starting standby check on %s with power=%d", d->name, power);
	d->state = Standby_Check;
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

	// check flags
	if (!PSTATE_CHECK_STANDBY || !PSTATE_STABLE || !PSTATE_VALID || PSTATE_DISTORTION || pstate->pv < BASELOAD * 2)
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
		d->timer = 0;
		if (pstate->soc < 1000)
			sleep(WAIT_AKKU_RAMP); // delay next round to give akku time to adjust
		return 0;
	}

	// standby check was negative - we got a response
	if (d->state == Standby_Check && response) {
		xdebug("FRONIUS standby check negative for %s, delta load expected %d actual %d", d->name, expected, delta);
		d->noresponse = 0;
		d->state = Active;
		d->timer = 0;
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
	if (expected > 0) {
		if (pstate->soc < 1000)
			sleep(WAIT_AKKU_RAMP); // delay next round to give akku time to adjust
		return 0;
	}

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

	// mosmix 24h raw values forecasts today, tomorrow and tomorrow+1
	mosmix_csv_t m0, m1, m2;
	mosmix_24h(0, &m0);
	mosmix_24h(1, &m1);
	mosmix_24h(2, &m2);
	xdebug(MOSMIX3X24, m0.Rad1h, m0.SunD1, m0.RSunD, m1.Rad1h, m1.SunD1, m1.RSunD, m2.Rad1h, m2.SunD1, m2.RSunD);

	// update produced energy this hour and recalculate mosmix factors for each mppt
	mosmix_mppt(now, gstate->mppt1, gstate->mppt2, gstate->mppt3, gstate->mppt4);

	// collect total expected today, tomorrow and till end of day / start of day
	int today, tomorrow, sod, eod;
	mosmix_collect(now, &today, &tomorrow, &sod, &eod);
	gstate->today = today;
	gstate->tomorrow = tomorrow;
	gstate->expected = eod;

	// calculate survival factor
	int hours, from, to;
	mosmix_survive(now, BASELOAD / 2, &hours, &from, &to);
	int available = gstate->expected + gstate->akku;
	int needed = collect_load(from, hours);
	float survive = needed ? (float) available / (float) needed : 0.0;
	gstate->survive = survive * 10; // store as x10 scaled
	xdebug("FRONIUS survive needed=%d available=%d (%d expected + %d akku) --> %.2f", needed, available, gstate->expected, gstate->akku, survive);

	// calculate heating factor
	int heating_total = collect_heating_total();
	mosmix_heating(now, heating_total, &hours, &from, &to);
	int needed_heating = heating_total * hours;
	int remaining = gstate->expected - survive;
	float heating = needed_heating && remaining > 0 ? (float) (remaining) / (float) needed_heating : 0.0;
	// float heating = needed ? (float) available / (float) needed : 0.0;
	gstate->heating = heating * 10; // store as x10 scaled
	xdebug("FRONIUS heating needed=%d expected=%d --> %.2f", needed_heating, gstate->expected, heating);

	// actual vs. yesterdays expected ratio
	int actual = 0;
	for (int i = 0; i <= now->tm_hour; i++)
		actual += GSTATE_HOUR(i)->pv;
	int yesterdays_tomorrow = GSTATE_HOUR(23)->tomorrow;
	float error = yesterdays_tomorrow ? (float) actual / (float) yesterdays_tomorrow : 0;
	xdebug("FRONIUS yesterdays forecast for today %d, actual %d, error %.2f", yesterdays_tomorrow, actual, error);

	// dump todays history
	mosmix_dump_history_today(now);
}

static void calculate_gstate() {
	// take over raw values
	gstate->soc = r->soc * 10.0; // store value as promille 0/00
	counter->mppt1 = (r->pv10_total1 / 3600) + (r->pv10_total2 / 3600); // counters are in Ws!
	counter->produced = r->produced;
	counter->consumed = r->consumed;
	if (r->pv7_total > 0.0)
		counter->mppt3 = r->pv7_total; // don't take over zero as Fronius7 might be in sleep mode

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
	int range_ok = gstate->soc > 100 && gstate->soc < 900 && g->soc > 100 && g->soc < 900;
	gstate->akku = gstate->soc > MIN_SOC ? AKKU_CAPACITY_SOC(gstate->soc - MIN_SOC) : 0;
	gstate->dakku = range_ok ? AKKU_CAPACITY_SOC(gstate->soc - g->soc) : 0;
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

static void calculate_pstate1() {
	// clear all flags
	pstate->flags = 0;

	// take over raw values from Fronius10
	pstate->akku = r->akku;
	pstate->grid = r->grid;
	pstate->load = r->load;
	pstate->mppt1 = r->pv10;
	pstate->soc = r->soc * 10.0;

	// get 2x history back
	pstate_t *h1 = get_pstate_history(-1);
	pstate_t *h2 = get_pstate_history(-2);

	// offline mode when 3x not enough PV production
	if (pstate->mppt1 < NOISE && h1->mppt1 < NOISE && h2->mppt1 < NOISE) {
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

	// emergency shutdown when three times extreme akku discharge or grid download
	if (pstate->akku > EMERGENCY && h1->akku > EMERGENCY && h2->akku > EMERGENCY && pstate->grid > EMERGENCY && h1->grid > EMERGENCY && h2->grid > EMERGENCY) {
		pstate->flags |= FLAG_EMERGENCY;
		return;
	}

	pstate->flags |= FLAG_VALID;
}

static void calculate_pstate2() {
	// take over raw values from Fronius7
	pstate->mppt3 = r->pv7;

	// clear VALID flag
	pstate->flags &= ~FLAG_VALID;

	// get 2x history back
	pstate_t *h1 = get_pstate_history(-1);
	pstate_t *h2 = get_pstate_history(-2);

	// total pv from both inverters
	pstate->pv = pstate->mppt1 + pstate->mppt2 + pstate->mppt3 + pstate->mppt4;
	pstate->dpv = pstate->pv - h1->pv;

	// subtract PV produced by Fronius7
	pstate->load -= (pstate->mppt3 + pstate->mppt4);

	// calculate delta load
	pstate->dload = pstate->load - h1->load;
	if (abs(pstate->dload) < NOISE)
		pstate->dload = 0;

	// calculate load manually
//	pstate->cload = (pstate->pv + pstate->akku + pstate->grid) * -1;

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
	// - expected load
	// - active devices
	// - all devices in standby
	pstate->flags |= FLAG_ALL_STANDBY;
	pstate->xload = BASELOAD;
	for (device_t **dd = DEVICES; *dd; dd++) {
		pstate->xload += DD->load;
		if (DD->power > 0 && DD != AKKU) // excl. akku; -1 when unitialized!
			pstate->flags |= FLAG_ACTIVE;
		if (DD->state != Standby)
			pstate->flags &= ~FLAG_ALL_STANDBY;
	}
	pstate->xload *= -1;

	// indicate standby check when deviation between actual load and calculated load is three times above 33%
	pstate->dxload = pstate->load < -BASELOAD ? (pstate->xload - pstate->load) * 100 / pstate->xload : 0;
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

	shape_pstate();
}

static int calculate_next_round(device_t *d) {
	if (d)
		return d->timer;

	if (PSTATE_OFFLINE || PSTATE_BURNOUT)
		return WAIT_OFFLINE;

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

static void emergency() {
	xlog("FRONIUS emergency shutdown at akku=%d grid=%d ", pstate->akku, pstate->grid);
	for (device_t **dd = DEVICES; *dd; dd++)
		ramp_device(DD, DOWN);
}

static void offline() {
	xlog("FRONIUS offline soc=%.1f temp=%.1f", FLOAT10(pstate->soc), TEMP_IN);
}

// burn out akku between 7 and 9 o'clock if we can re-charge it completely by day
static void burnout() {
	xlog("FRONIUS burnout soc=%.1f temp=%.1f", FLOAT10(pstate->soc), TEMP_IN);
	fronius_override_seconds("plug5", WAIT_OFFLINE);
	fronius_override_seconds("plug6", WAIT_OFFLINE);
	// fronius_override_seconds("plug7", WAIT_OFFLINE); // makes no sense due to ventilate sleeping room
	fronius_override_seconds("plug8", WAIT_OFFLINE);
}

static void daily(time_t now_ts) {
	xlog("FRONIUS executing daily tasks...");

	// aggregate 24 gstate hours into one day
	gstate_t gd;
	aggregate((int*) &gd, (int*) GSTATE_TODAY, GSTATE_SIZE, 24);
	dump_table((int*) GSTATE_TODAY, GSTATE_SIZE, 24, -1, "FRONIUS gstate_hours", GSTATE_HEADER);
	dump_struct((int*) &gd, GSTATE_SIZE, "[ØØ]", 0);

	// dump high noon mosmix slots
	mosmix_dump_history_noon();
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

	// update raw values
	errors += curl_perform(curl_readable, &memory, &parse_readable);

	// recalculate gstate, mosmix, then choose potd
	calculate_gstate();
	calculate_mosmix();
	choose_program();

	// copy counter and gstate to next slot (Fronius7 goes into sleep mode - no updates overnight)
	memcpy(COUNTER_NEXT, (void*) counter, sizeof(counter_t));
	memcpy(GSTATE_NEXT, (void*) gstate, sizeof(counter_t));

	// print actual gstate
	dump_table((int*) GSTATE_TODAY, GSTATE_SIZE, 24, now->tm_hour, "FRONIUS gstate_hours", GSTATE_HEADER);
	print_gstate(NULL);

#ifndef FRONIUS_MAIN
	// store to disk at 0, 6, 12, 18
	if (now->tm_hour % 5 == 0) {
		store_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));
		store_blob(COUNTER_FILE, counter_hours, sizeof(counter_hours));
		mosmix_store_state();
	}
#endif
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
	counter = COUNTER_NOW;
	gstate = GSTATE_NOW;
	curl_perform(curl_readable, &memory, &parse_readable);
	calculate_gstate();
	calculate_mosmix();
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

		// update state and counter pointers
		counter = COUNTER_NOW;
		gstate = GSTATE_NOW;

		// check error counter
		if (errors > 10)
			for (device_t **dd = DEVICES; *dd; dd++)
				ramp_device(DD, DOWN);

		// make Fronius10 API call and calculate first pstate
		errors += curl_perform(curl10, &memory, &parse_fronius10);
		calculate_pstate1();

		// check emergency
		if (PSTATE_EMERGENCY)
			emergency();

		// check offline
		if (PSTATE_OFFLINE)
			offline();

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

		// prio3: ramp up/down
		if (!device)
			device = ramp();

		// prio4: check if higher priorized device can steal from lower priorized
		if (!device)
			device = steal();

		// print combined device and pstate when we had delta or device action
		if (PSTATE_DELTA || device)
			print_state(device);

		// determine wait for next round
		wait = calculate_next_round(device);

		// print pstate history and device state when we have active devices
		if (PSTATE_ACTIVE)
			dump_table((int*) pstate_history, PSTATE_SIZE, 3, -1, "FRONIUS pstate_hours", PSTATE_HEADER);

		// set history pointer to next slot
		bump_pstate();
		errors = 0;
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

	ZERO(pstate_history);
	ZERO(gstate_hours);
	ZERO(counter_hours);
	ZEROP(r);

	load_blob(COUNTER_FILE, counter_hours, sizeof(counter_hours));
	load_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));
	mosmix_load_state();

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
	mosmix_store_state();
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
	ramp_device(d, d->total);

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
	if (power > 0 && power < heater->total)
		return 0; // continue loop

	// transform power into on/off
	power = power > 0 ? 1 : 0;

	// ignore ramp ups as long as we have distortion or unstable
	if (power && PSTATE_DISTORTION)
		return 0; // continue loop

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
	int step = power / (boiler->total / 100);
	if (step > 0 && PSTATE_DISTORTION)
		step /= 2; // smaller up steps when we have distortion
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
	boiler->power = power;
	boiler->load = boiler->total * boiler->power / 100;
	boiler->xload = boiler->total * step / -100;
	boiler->aload = pstate ? pstate->load : 0;
	boiler->timer = WAIT_RESPONSE;
	return 1; // loop done
}

int ramp_akku(device_t *akku, int power) {

	// disable response/standby logic
	akku->load = akku->xload = 0;
	akku->power = 1; // always enabled

	// TODO untested logic

	if (power) {

		// allow akku charging by recalculate ramp power only from grid
		pstate->ramp = (pstate->grid) * -1;
		if (!PSTATE_ACTIVE && pstate->ramp < 0)
			pstate->ramp = 0; // no active devices - nothing to ramp down
		if (0 < pstate->ramp && pstate->ramp < RAMP_WINDOW) // stable between 0..25
			pstate->ramp = 0;

	} else {

		// force akku to release charging power by recalculate ramp power from grid and akku
		pstate->ramp = (pstate->grid + pstate->akku) * -1;
		if (!PSTATE_ACTIVE && pstate->ramp < 0)
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
