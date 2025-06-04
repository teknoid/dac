#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <ansi-color-codes.h>

#include "tasmota.h"
#include "mosmix.h"
#include "solar.h"
#include "mcp.h"

//#include "solar-tron25-modbus.h"
#include "solar-tron25-api.h"

#ifndef TEMP_IN
#define TEMP_IN					22.0
#endif

#ifndef TEMP_OUT
#define TEMP_OUT				15.0
#endif

static void create_pstate_json() {
	store_struct_json((int*) pstate, PSTATE_SIZE, PSTATE_HEADER, PSTATE_JSON);
}

static void create_gstate_json() {
	store_struct_json((int*) gstate, GSTATE_SIZE, GSTATE_HEADER, GSTATE_JSON);
}

// feed Fronius powerflow web application
static void create_powerflow_json() {
	FILE *fp = fopen(POWERFLOW_JSON, "wt");
	if (fp == NULL)
		return;

	fprintf(fp, POWERFLOW_TEMPLATE, FLOAT10(pstate->soc), pstate->akku, pstate->grid, pstate->load, pstate->pv);
	fflush(fp);
	fclose(fp);
}

// devices
static void create_dstate_json() {
	FILE *fp = fopen(DSTATE_JSON, "wt");
	if (fp == NULL)
		return;

	int i = 0;
	fprintf(fp, "[");
	for (device_t **dd = potd->devices; *dd; dd++) {
		if (i++)
			fprintf(fp, ",");
		if (DD == AKKU)
			fprintf(fp, DSTATE_TEMPLATE, DD->name, DD->state, DD->power, AKKU_CHARGE_MAX, pstate->akku);
		else
			fprintf(fp, DSTATE_TEMPLATE, DD->name, DD->state, DD->power, DD->total, DD->load);
	}

	fprintf(fp, "]");
	fflush(fp);
	fclose(fp);
}

static void plot() {
	// create gstate daily/weekly
	store_table_csv((int*) GSTATE_TODAY, GSTATE_SIZE, 24, GSTATE_HEADER, GSTATE_TODAY_CSV);
	store_table_csv((int*) gstate_hours, GSTATE_SIZE, 24 * 7, GSTATE_HEADER, GSTATE_WEEK_CSV);

	// create mosmix history, today, tomorrow csv
	mosmix_store_csv();

	// plot diagrams
	system(GNUPLOT);
}

static device_t* get_by_name(const char *name) {
	for (device_t **dd = DEVICES; *dd; dd++)
		if (!strcmp(DD->name, name))
			return DD;

	return 0;
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
				xdebug("SOLAR suspicious collect_loads day=%d hour=%d load=%d --> using BASELOAD", d, h, load);
				load = BASELOAD;
			}
			loads[h] += load;
		}
		loads[h] /= 7;
	}

	strcpy(line, "SOLAR average 24/7 loads:");
	for (int h = 0; h < 24; h++) {
		snprintf(value, 10, " %d", loads[h]);
		strcat(line, value);
	}
	xdebug(line);

#ifndef SOLAR_MAIN
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
			xdebug("SOLAR Override expired for %s", d->name);
			d->override = 0;
			power = 0;
		} else {
			xdebug("SOLAR Override active for %lu seconds on %s", d->override - t, d->name);
			power = d->adj ? 100 : 1;
		}
	}
	return power;
}

static void print_gstate() {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "SOLAR");
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
	xlogl_start(line, "SOLAR  ");

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
	inverter_status(line);
	if (lock)
		xlogl_int(line, "   Lock", lock);
	xlogl_end(line, strlen(line), 0);
}

// call device specific ramp function
static int ramp(device_t *d, int power) {
	if (power < 0)
		xdebug("SOLAR ramp↓ %d %s", power, d->name);
	else if (power > 0)
		xdebug("SOLAR ramp↑ +%d %s", power, d->name);
	else
		xdebug("SOLAR ramp 0 %s", d->name);

	// set/reset response lock
	int ret = (d->ramp)(d, power);
	if (lock < ret)
		lock = ret;

	return ret;
}

static int select_program(const potd_t *p) {
	if (potd == p)
		return 0;

	// potd has changed - reset all devices (except AKKU) and set AKKU to initial state
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD != AKKU)
			ramp(DD, DOWN);
	if (AKKU_CHARGING)
		AKKU->power = -1;

	xlog("SOLAR selecting %s program of the day", p->name);
	potd = (potd_t*) p;
	lock = WAIT_RESPONSE;

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
	akku_discharge(); // enable discharge
	for (device_t **dd = DEVICES; *dd; dd++)
		ramp(DD, DOWN);
	xlog("SOLAR emergency shutdown at akku=%d grid=%d ", pstate->akku, pstate->grid);
}

static void burnout() {
	akku_discharge(); // enable discharge
	solar_override_seconds("küche", WAIT_BURNOUT);
	solar_override_seconds("wozi", WAIT_BURNOUT);
	xlog("SOLAR burnout soc=%.1f temp=%.1f", FLOAT10(gstate->soc), TEMP_IN);
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
		if (PSTATE_MIN_LAST1->ramp < 1000)
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
			xdebug("SOLAR %s steal %d (min=%d)", DD->name, p, min);
			return DD;
		}
	}

	return 0;
}

static device_t* perform_standby(device_t *d) {
	int power = d->adj ? (d->power < 50 ? +500 : -500) : (d->power ? d->total * -1 : d->total);
	xdebug("SOLAR starting standby check on %s with power=%d", d->name, power);
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

	// valid response is at least 2/3 of last ramp
	int delta = d->delta - d->delta / 3;
	d->delta = 0; // reset

	// check if we got a response on any phase
	int r1 = delta > 0 ? d1 > delta : d1 < delta;
	int r2 = delta > 0 ? d2 > delta : d2 < delta;
	int r3 = delta > 0 ? d3 > delta : d3 < delta;
	int r = r1 || r2 || r3;

	// load is completely satisfied from secondary inverter
	int extra = pstate->ac2 > pstate->load * -1;

	// wait more to give akku time to release power when ramped up
	int wait = AKKU_CHARGING && delta > 0 && !extra ? 3 * WAIT_RESPONSE : 0;

	// response OK
	if (r && (d->state == Active || d->state == Active_Checked)) {
		xdebug("SOLAR response OK from %s, delta expected %d actual %d %d %d", d->name, delta, d1, d2, d3);
		d->noresponse = 0;
		lock = wait;
		return lock ? d : 0;
	}

	// standby check was negative - we got a response
	if (d->state == Standby_Check && r) {
		xdebug("SOLAR standby check negative for %s, delta expected %d actual %d %d %d", d->name, delta, d1, d2, d3);
		d->noresponse = 0;
		d->state = Active_Checked; // mark Active with standby check performed
		lock = wait;
		return d; // recalculate in next round
	}

	// standby check was positive -> set device into standby
	if (d->state == Standby_Check && !r) {
		xdebug("SOLAR standby check positive for %s, delta expected %d actual %d %d %d  --> entering standby", d->name, delta, d1, d2, d3);
		ramp(d, d->total * -1);
		d->noresponse = d->delta = lock = 0; // no response from switch off expected
		d->state = Standby;
		return d; // recalculate in next round
	}

	// ignore standby check when power was released
	if (delta < 0)
		return 0;

	// perform standby check when noresponse counter reaches threshold
	if (++d->noresponse >= STANDBY_NORESPONSE)
		return perform_standby(d);

	xdebug("SOLAR no response from %s count %d/%d", d->name, d->noresponse, STANDBY_NORESPONSE);
	return 0;
}

static void calculate_gstate() {
	// clear state flags and values
	gstate->flags = 0;

	// take over SoC
	gstate->soc = pstate->soc;

	// store average load in gstate history
	gstate->load = PSTATE_HOUR_LAST1->load;

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
	xdebug("SOLAR success sod=%d pv=%d --> %.2f", gstate->sod, gstate->pv, success);

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
	xdebug("SOLAR survive eod=%d tocharge=%d available=%d akku=%d needed=%d --> %.2f", gstate->eod, tocharge, available, gstate->akku, gstate->need_survive, survive);

	// heating factor
	float heating = gstate->need_heating ? (float) available / (float) gstate->need_heating - 1.0 : 0;
	gstate->heating = heating * 100; // store as x100 scaled
	if (gstate->heating > 1000)
		gstate->heating = 1000;
	xdebug("SOLAR heating eod=%d tocharge=%d available=%d needed=%d --> %.2f", gstate->eod, tocharge, available, gstate->need_heating, heating);

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
		xdebug("SOLAR heating enabled month=%d temp_in=%d temp_ou=%d", now->tm_mon, TEMP_IN, TEMP_OUT);
}

static void calculate_pstate() {
	// clear state flags and values
	pstate->flags = pstate->ramp = 0;

	// clear delta sum counters every minute
	if (now->tm_sec == 0)
		pstate->sdpv = pstate->sdgrid = pstate->sdload = 0;

	// get history states
	pstate_t *s1 = PSTATE_SEC_LAST1;
	pstate_t *s2 = PSTATE_SEC_LAST2;
	pstate_t *m1 = PSTATE_MIN_LAST1;
	pstate_t *m2 = PSTATE_MIN_LAST2;

	// total PV produced by all strings
	if (pstate->mppt1 == 1)
		pstate->mppt1 = 0; // noise
	if (pstate->mppt2 == 1)
		pstate->mppt2 = 0; // noise
	if (pstate->mppt3 == 1)
		pstate->mppt3 = 0; // noise
	if (pstate->mppt4 == 1)
		pstate->mppt4 = 0; // noise
	pstate->pv = pstate->mppt1 + pstate->mppt2 + pstate->mppt3 + pstate->mppt4;
	pstate->dpv = pstate->pv - s1->pv;
	if (abs(pstate->dpv) < NOISE)
		pstate->dpv = 0; // shape dgrid
	pstate->sdpv += abs(pstate->dpv);

	// grid, delta grid and sum
	pstate->dgrid = pstate->grid - s1->grid;
	if (abs(pstate->dgrid) < NOISE)
		pstate->dgrid = 0; // shape dgrid
	pstate->sdgrid += abs(pstate->dgrid);

	// load, delta load + sum
	pstate->load = (pstate->ac1 + pstate->ac2 + pstate->grid) * -1;
	pstate->dload = pstate->load - s1->load;
	if (abs(pstate->dload) < NOISE)
		pstate->dload = 0; // shape dload
	pstate->sdload += abs(pstate->dload);

	// check if we have delta ac power anywhere
	if (abs(pstate->grid - s1->grid) > NOISE)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac1 - s1->ac1) > NOISE)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac2 - s1->ac2) > NOISE)
		pstate->flags |= FLAG_DELTA;

	// offline mode when not enough PV production
	int online = pstate->pv > MINIMUM || m1->pv > MINIMUM || m2->pv > MINIMUM;
	if (!online) {
		// akku burn out between 6 and 9 o'clock if we can re-charge it completely by day
		int burnout_time = now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8;
		int burnout_possible = TEMP_IN < 18 && pstate->soc > 150;
		if (burnout_time && burnout_possible && AKKU_BURNOUT)
			pstate->flags |= FLAG_BURNOUT; // burnout
		else
			pstate->flags |= FLAG_OFFLINE; // offline
		return;
	}

	// emergency shutdown when 3x extreme grid download or last minute big akku discharge or grid download
	int e_short = EMERGENCY && pstate->grid > EMERGENCY * 2 && s1->grid > EMERGENCY * 2 && s2->grid > EMERGENCY * 2;
	int e_long = EMERGENCY && (m1->akku > EMERGENCY || m1->grid > EMERGENCY);
	if (e_short || e_long) {
		pstate->flags |= FLAG_EMERGENCY;
		return;
	}

	// no further calculations while lock active
	if (lock)
		return;

	// first set and then clear VALID flag when values suspicious
	pstate->flags |= FLAG_VALID;
	int sum = pstate->grid + pstate->akku + pstate->load + pstate->pv;
	if (abs(sum) > SUSPICIOUS) { // probably inverter power dissipations (?)
		xdebug("SOLAR suspicious values detected: sum=%d", sum);
		pstate->flags &= ~FLAG_VALID;
	}
	if (pstate->load > 0) {
		xdebug("SOLAR positive load detected");
		pstate->flags &= ~FLAG_VALID;
	}
	if (pstate->grid < -NOISE && pstate->akku > NOISE) {
		int waste = abs(pstate->grid) < pstate->akku ? abs(pstate->grid) : pstate->akku;
		xdebug("SOLAR wasting power %d akku -> grid", waste);
		pstate->flags &= ~FLAG_VALID;
	}
	if (pstate->dgrid > BASELOAD * 2) { // e.g. refrigerator starts !!!
		xdebug("SOLAR grid spike detected %d: %d -> %d", pstate->grid - s1->grid, s1->grid, pstate->grid);
		pstate->flags &= ~FLAG_VALID;
	}

	// validate inverter status
	inverter_valid();

	// no further calculations while invalid
	if (!PSTATE_VALID) {
		lock = WAIT_INVALID;
		return;
	}

	// state is stable when we have 3x no grid changes
	if (!pstate->dgrid && !s1->dgrid && !s2->dgrid)
		pstate->flags |= FLAG_STABLE;

	// device loop:
	// - xload/dxload
	// - all devices up/down/standby
	pstate->xload = 0;
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

	// ramp up/down power
	pstate->ramp = pstate->grid * -1;
	if (pstate->akku > NOISE)
		pstate->ramp -= pstate->akku;
	// stable when grid between -RAMP_WINDOW..+NOISE
	if (-RAMP_WINDOW < pstate->grid && pstate->grid <= NOISE)
		pstate->ramp = 0;
	// ramp down as soon as grid goes above NOISE
	if (NOISE < pstate->grid && pstate->grid <= RAMP_WINDOW)
		pstate->ramp = -RAMP_WINDOW;
	// when akku is charging it regulates around 0, so set stable window between -RAMP_WINDOW..+RAMP_WINDOW
	if (pstate->akku < -NOISE && -RAMP_WINDOW < pstate->grid && pstate->grid <= RAMP_WINDOW)
		pstate->ramp = 0;
	// 50% more ramp down when PV tendency is falling
	if (pstate->ramp < 0 && m1->dpv < 0)
		pstate->ramp += pstate->ramp / 2;
	// delay ramp up as long as average PV is below average load
	int m1load = (m1->load + m1->load / 10) * -1; // + 10%;
	if (pstate->ramp > 0 && m1->pv < m1load) {
		xdebug("SOLAR delay ramp up as long as average pv %d is below average load %d", m1->pv, m1load);
		pstate->ramp = 0;
	}

	// distortion when current sdpv is too big or aggregated last two sdpv's are too big
	int d0 = pstate->sdpv > m1->pv;
	int d1 = m1->sdpv > m1->pv + m1->pv / 2;
	int d2 = m2->sdpv > m2->pv + m2->pv / 2;
	if (d0 || d1 || d2)
		pstate->flags |= FLAG_DISTORTION;
	if (PSTATE_DISTORTION)
		xdebug("SOLAR set FLAG_DISTORTION 0=%d/%d 1=%d/%d 2=%d/%d", pstate->sdpv, pstate->pv, m1->sdpv, m1->pv, m2->sdpv, m2->pv);

	// indicate standby check when actual load is 3x below 50% of calculated load
	if (pstate->xload && pstate->dxload < 50 && s1->dxload < 50 && s2->dxload < 50)
		pstate->flags |= FLAG_CHECK_STANDBY;
	if (PSTATE_CHECK_STANDBY)
		xdebug("SOLAR set FLAG_CHECK_STANDBY load=%d xload=%d dxload=%d", pstate->load, pstate->xload, pstate->dxload);
}

static void daily() {
	PROFILING_START
	xlog("SOLAR executing daily tasks...");

	// recalculate average 24/7 loads
	collect_loads();

	// calculate forecast errors - actual vs. expected
	int forecast_yesterday = GSTATE_YDAY_HOUR(23)->tomorrow;
	float eyesterday = forecast_yesterday ? (float) gstate->pv / (float) forecast_yesterday : 0;
	xdebug("SOLAR yesterdays forecast for today %d, actual %d, error %.2f", forecast_yesterday, gstate->pv, eyesterday);
	int forecast_today = GSTATE_HOUR(6)->today;
	float etoday = forecast_today ? (float) gstate->pv / (float) forecast_today : 0;
	xdebug("SOLAR today's 04:00 forecast for today %d, actual %d, error %.2f", forecast_today, gstate->pv, etoday);

	// recalculate mosmix factors
	mosmix_factors();

#ifndef SOLAR_MAIN
	store_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));
	store_blob(COUNTER_FILE, counter_hours, sizeof(counter_hours));
	store_blob(PSTATE_H_FILE, pstate_hours, sizeof(pstate_hours));
	store_blob(PSTATE_M_FILE, pstate_minutes, sizeof(pstate_minutes));
#endif

	// recalculate gstate
	calculate_gstate();
	print_gstate();

	PROFILING_LOG("SOLAR daily");
}

static void hourly() {
	PROFILING_START
	xlog("SOLAR executing hourly tasks...");

	// copy gstate and counters to history
	memcpy(COUNTER_NOW, (void*) counter, sizeof(counter_t));
	memcpy(GSTATE_NOW, (void*) gstate, sizeof(gstate_t));

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

	// set akku storage strategy
	akku_strategy();

#ifndef SOLAR_MAIN
	// we need the history in mosmix.c main()
	mosmix_store_history();

	// create/append pstate minutes csv
	int offset = 60 * (now->tm_hour > 0 ? now->tm_hour - 1 : 23);
	if (!offset || access(PSTATE_M_CSV, F_OK))
		store_csv_header(PSTATE_HEADER, PSTATE_M_CSV);
	append_table_csv((int*) pstate_minutes, PSTATE_SIZE, 60, offset, PSTATE_M_CSV);

	// workaround /run/mcp get's immediately deleted at stop/kill
	xlog("SOLAR saving runtime directory: %s", SAVE_RUN_DIRECORY);
	system(SAVE_RUN_DIRECORY);
#endif

	// paint new diagrams
	plot();

	PROFILING_LOG("SOLAR hourly");
}

static void minly() {
	// xlog("SOLAR minly m1pv=%d m1grid=%d m1load=%d", PSTATE_MIN_LAST1->pv, PSTATE_MIN_LAST1->grid, PSTATE_MIN_LAST1->load);

	// enable discharge if we have grid download
	if (pstate->grid > NOISE && PSTATE_MIN_LAST1->grid > NOISE)
		akku_discharge();
}

static void aggregate_mhd() {
	// aggregate 60 seconds into one minute
	if (now->tm_sec == 0) {
		aggregate((int*) PSTATE_MIN_LAST1, (int*) pstate_seconds, PSTATE_SIZE, 60);
		// dump_table((int*) pstate_seconds, PSTATE_SIZE, 60, -1, "SOLAR pstate_seconds", PSTATE_HEADER);
		// dump_struct((int*) PSTATE_MIN_LAST1, PSTATE_SIZE, "[ØØ]", 0);

		// aggregate 60 minutes into one hour
		if (now->tm_min == 0) {
			aggregate((int*) PSTATE_HOUR_LAST1, (int*) pstate_minutes, PSTATE_SIZE, 60);
			// dump_table((int*) pstate_minutes, PSTATE_SIZE, 60, -1, "SOLAR pstate_minutes", PSTATE_HEADER);
			// dump_struct((int*) PSTATE_HOUR_LAST1, PSTATE_SIZE, "[ØØ]", 0);

			// aggregate 24 pstate/gstat hours into one day
			if (now->tm_hour == 0) {
				pstate_t pda, pdc;
				aggregate((int*) &pda, (int*) pstate_hours, PSTATE_SIZE, 24);
				cumulate((int*) &pdc, (int*) pstate_hours, PSTATE_SIZE, 24);
				dump_table((int*) pstate_hours, PSTATE_SIZE, 24, -1, "SOLAR pstate_hours", PSTATE_HEADER);
				dump_struct((int*) &pda, PSTATE_SIZE, "[ØØ]", 0);
				dump_struct((int*) &pdc, PSTATE_SIZE, "[++]", 0);

				gstate_t gda, gdc;
				aggregate((int*) &gda, (int*) GSTATE_YDAY, GSTATE_SIZE, 24);
				cumulate((int*) &gdc, (int*) GSTATE_YDAY, GSTATE_SIZE, 24);
				dump_table((int*) GSTATE_YDAY, GSTATE_SIZE, 24, -1, "SOLAR gstate_hours", GSTATE_HEADER);
				dump_struct((int*) &gda, GSTATE_SIZE, "[ØØ]", 0);
				dump_struct((int*) &gdc, GSTATE_SIZE, "[++]", 0);
			}
		}
	}
}

static void solar() {
	time_t now_ts;
	device_t *device = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// the SOLAR main loop
	while (1) {
		// PROFILING_START

		// get actual time
		now_ts = time(NULL);
		localtime_r(&now_ts, now);

		// aggregate values into minute-hour-day when 0-0-0
		aggregate_mhd();

		// calculate pstate and store to history in a mutex
		pthread_mutex_lock(&pstate_lock);
		calculate_pstate();
		memcpy(PSTATE_NOW, (void*) pstate, sizeof(pstate_t));
		pthread_mutex_unlock(&pstate_lock);

		// startup and every 10 minutes: calculate gstate and potd
		if (!potd || (now->tm_sec == 0 && now->tm_min % 10 == 0)) {
			calculate_gstate();
			choose_program();
			print_gstate();
		}

		// no actions until lock is expired
		if (lock)
			lock--;

		else {

			// emergency mode
			if (PSTATE_EMERGENCY)
				emergency();

			// burnout mode
			if (PSTATE_BURNOUT)
				burnout();

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
		}

		// print pstate once per minute / when delta / on device action / on grid load
		if (PSTATE_DELTA || device || now->tm_sec == 0 || pstate->grid > NOISE)
			print_pstate_dstate(device);

		// minutely tasks
		if (now->tm_sec == 0) {
			minly();

			// hourly tasks
			if (now->tm_min == 0) {
				hourly();

				// daily tasks
				if (now->tm_hour == 0)
					daily();
			}
		}

		// web output
		create_pstate_json();
		create_dstate_json();
		create_gstate_json();
		create_powerflow_json();

		// PROFILING_LOG("SOLAR main loop")

		// wait for next second
		while (now_ts == time(NULL))
			msleep(100);
	}
}

static int init() {
//	set_debug(1);

	pthread_mutex_init(&pstate_lock, NULL);

	// create a socket for sending UDP messages
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == 0)
		return xerr("Error creating socket");

	// initialize time structure
	time_t now_ts = time(NULL);
	localtime_r(&now_ts, now);

	// initialize all devices with start values
	xlog("SOLAR initializing devices");
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

	mosmix_load_history(now);
	mosmix_factors();
	mosmix_load(now, MARIENBERG, 0);
	collect_loads();
	plot();

	// create empty minutes CSV file if not found
	if (access(PSTATE_M_CSV, F_OK))
		store_csv_header(PSTATE_HEADER, PSTATE_M_CSV);

	// call implementation specific init() function
	if (solar_init() != 0)
		return -1;

	// wait for collecting models and generating first pstate data
	sleep(5);

	return 0;
}

static void stop() {
	// call implementation specific stop() function
	solar_stop();

#ifndef SOLAR_MAIN
	store_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));
	store_blob(COUNTER_FILE, counter_hours, sizeof(counter_hours));
	store_blob(PSTATE_H_FILE, pstate_hours, sizeof(pstate_hours));
	store_blob(PSTATE_M_FILE, pstate_minutes, sizeof(pstate_minutes));
	mosmix_store_history();
#endif

	if (sock)
		close(sock);

	pthread_mutex_destroy(&pstate_lock);
}

// run the main loop forever
static int loop() {
	init();
	solar();
	pause();
	stop();
	return 0;
}

// do all calculations in one single round trip and exit
static int single() {
	init();

	aggregate_mhd();
	collect_loads();
	calculate_pstate();
	calculate_gstate();
	choose_program();
	response(&b1);
	standby();
	rampdown();
	rampup();
	steal();

	print_gstate();
	print_pstate_dstate(NULL);

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
	// initialize hourly & daily & monthly
	time_t now_ts = time(NULL);
	localtime_r(&now_ts, now);

	for (int i = 0; i < 60; i++)
		pstate_seconds[i].pv = i + 10;

	for (int i = 0; i < 60; i++)
		printf("%d ", pstate_seconds[i].pv);
	printf("\n");

	now->tm_sec = 1;
	printf("%d\n", PSTATE_NOW->pv);
	printf("%d\n", PSTATE_SEC_LAST1->pv);
	printf("%d\n", PSTATE_SEC_LAST2->pv);
	printf("%d\n", PSTATE_SEC_LAST3->pv);

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

static int migrate() {
	gstate_old_t old[24 * 7];
	ZERO(old);
	load_blob("/tmp/solar-gstate.bin", old, sizeof(old));

	for (int i = 0; i < 24 * 7; i++) {
		gstate_old_t *o = &old[i];
		gstate_t *n = &gstate_hours[i];

		n->pv = o->pv;
		n->produced = o->produced;
		n->consumed = o->consumed;
		n->today = o->today;
		n->tomorrow = o->tomorrow;
		n->eod = o->eod;
		n->load = o->load;
		n->soc = o->soc;
		n->akku = o->akku;
		n->ttl = o->ttl;
		n->survive = o->survive;
		n->heating = o->heating;
		n->success = o->success;
	}
	store_blob(GSTATE_FILE, gstate_hours, sizeof(gstate_hours));
	return 0;
}

int solar_boiler1() {
	return select_program(&BOILER1);
}

int solar_boiler3() {
	return select_program(&BOILER3);
}

int solar_override_seconds(const char *name, int seconds) {
	device_t *d = get_by_name(name);
	if (!d)
		return 0;
	if (d->override)
		return 0;

	xlog("SOLAR Activating Override on %s", d->name);
#ifdef SOLAR_MAIN
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

int solar_override(const char *name) {
	return solar_override_seconds(name, OVERRIDE);
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
		xdebug("SOLAR switching %s ON", heater->name);
	else
		xdebug("SOLAR switching %s OFF", heater->name);

#ifndef SOLAR_MAIN
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

	// summer: charging boiler3 only between 11 and 15 o'clock
	if (boiler == B3 && power > 0 && (now->tm_hour < 11 || now->tm_hour >= 15) && SUMMER) {
		boiler->state = Standby;
		return 0; // continue loop
	}

	// send UDP message to device
	char message[16];
	snprintf(message, 16, "p:%d:%d", power, 0);

	if (step < 0)
		xdebug("SOLAR ramp↓ %s step %d UDP %s", boiler->name, step, message);
	else
		xdebug("SOLAR ramp↑ %s step +%d UDP %s", boiler->name, step, message);

#ifndef SOLAR_MAIN
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
	// init - set to discharge when offline, otherwise to standby
	if (akku->power == -1)
		return PSTATE_OFFLINE ? akku_discharge() : akku_standby();

	// ramp down request
	if (power < 0) {

		// consume ramp down up to current charging power
		akku->delta = power < pstate->akku ? pstate->akku : power;

		// skip ramp downs as long as we don't have grid download (akku ramps down itself / forward to next device)
		if (PSTATE_MIN_LAST1->grid < NOISE)
			return AKKU_CHARGING ? 1 : 0;

		// enable discharging
		return akku_discharge();
	}

	// ramp up request
	if (power > 0) {

		// expect to consume all DC power up to battery's charge maximum
		akku->delta = (pstate->mppt1 + pstate->mppt2) < AKKU_CHARGE_MAX ? (pstate->mppt1 + pstate->mppt2) : AKKU_CHARGE_MAX;

		// set into standby when full
		if (gstate->soc == 1000)
			return akku_standby();

		// forward to next device if we have grid upload in despite of charging
		if (PSTATE_MIN_LAST1->grid < -NOISE && AKKU_CHARGING)
			return 0; // continue loop

		// summer: charging starts at high noon when below 25%
		if ((gstate->soc > 250 || now->tm_hour < 12) && SUMMER)
			return 0; // continue loop

		// enable charging
		return akku_charge();
	}

	return 0;
}

int solar_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	// no arguments - run one single roundtrip
	if (argc == 1)
		return single();

	int c;
	while ((c = getopt(argc, argv, "b:c:o:s:fglmt")) != -1) {
		// printf("getopt %c\n", c);
		switch (c) {
		case 'b':
			// -X: limit charge, +X: limit dscharge, 0: no limits
			return battery(optarg);
		case 'c':
			// execute as: stdbuf -i0 -o0 -e0 ./solar -c boiler1 > boiler1.txt
			return calibrate(optarg);
		case 'o':
			return solar_override(optarg);
		case 's':
			return storage_min(optarg);
		case 'f':
			return fake();
		case 'g':
			return grid();
		case 'l':
			return loop();
		case 'm':
			return migrate();
		case 't':
			return test();
		default:
			xlog("unknown getopt %c", c);
		}
	}

	return 0;
}

#ifdef SOLAR_MAIN
int main(int argc, char **argv) {
	return solar_main(argc, argv);
}
#else
MCP_REGISTER(solar, 7, &init, &stop, &solar);
#endif
