#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include "solar-common.h"
#include "tasmota-devices.h"
#include "tasmota.h"
#include "sunspec.h"
#include "utils.h"
#include "mcp.h"

// JSON files for webui
#define DSTATE_JSON				"dstate.json"
#define DEVICES_JSON			"devices.json"

#define DSTATE_TEMPLATE			"{\"name\":\"%s\", \"state\":%d, \"power\":%d, \"total\":%d, \"load\":%d}"

#define DD						(*dd)
#define UP						(*dd)->total
#define DOWN					(*dd)->total * -1

#define AKKU_CHARGING			(AKKU->state == Charge)
#define AKKU_LIMIT_CHARGE		1750
#define AKKU_LIMIT_DISCHARGE	BASELOAD

#define OVERRIDE				600

#define STANDBY_NORESPONSE		5

// program of the day - choosen by mosmix forecast data
typedef struct potd_t {
	const char *name;
	device_t **devices;
} potd_t;
static potd_t *potd = 0;

// device ramp signatures
static int ramp_heater(device_t *device, int power);
static int ramp_boiler(device_t *device, int power);
static int ramp_akku(device_t *device, int power);

// devices
static device_t a1 = { .name = "akku", .total = 0, .ramp = &ramp_akku, .adj = 0 }, *AKKU = &a1;
static device_t b1 = { .name = "boiler1", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t b2 = { .name = "boiler2", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t b3 = { .name = "boiler3", .total = 2000, .ramp = &ramp_boiler, .adj = 1, .from = 11, .to = 15, .min = 5 };
static device_t h1 = { .name = "küche", .total = 500, .ramp = &ramp_heater, .adj = 0, .id = SWITCHBOX, .r = 1 };
static device_t h2 = { .name = "wozi", .total = 500, .ramp = &ramp_heater, .adj = 0, .id = SWITCHBOX, .r = 2 };
static device_t h3 = { .name = "schlaf", .total = 500, .ramp = &ramp_heater, .adj = 0, .id = PLUG5, .r = 0 };
static device_t h4 = { .name = "tisch", .total = 200, .ramp = &ramp_heater, .adj = 0, .id = SWITCHBOX, .r = 3, };

// all devices, needed for initialization
static device_t *DEVICES[] = { &a1, &b1, &b2, &b3, &h1, &h2, &h3, &h4, 0 };

// first charge akku, then boilers, then heaters
static device_t *DEVICES_MODEST[] = { &a1, &b1, &h1, &h2, &h3, &h4, &b2, &b3, 0 };

// steal all akku charge power
static device_t *DEVICES_GREEDY[] = { &h1, &h2, &h3, &h4, &b1, &b2, &b3, &a1, 0 };

// heaters, then akku, then boilers (catch remaining pv from secondary inverters or if akku is not able to consume all generated power)
static device_t *DEVICES_PLENTY[] = { &h1, &h2, &h3, &h4, &a1, &b1, &b2, &b3, 0 };

// force boiler heating first
static device_t *DEVICES_BOILERS[] = { &b1, &b2, &b3, &h1, &h2, &h3, &h4, &a1, 0 };
static device_t *DEVICES_BOILER1[] = { &b1, &a1, &b2, &b3, &h1, &h2, &h3, &h4, 0 };
static device_t *DEVICES_BOILER3[] = { &b3, &a1, &b1, &b2, &h1, &h2, &h3, &h4, 0 };

// define POTDs
static const potd_t MODEST = { .name = "MODEST", .devices = DEVICES_MODEST };
static const potd_t GREEDY = { .name = "GREEDY", .devices = DEVICES_GREEDY };
static const potd_t PLENTY = { .name = "PLENTY", .devices = DEVICES_PLENTY };
static const potd_t BOILERS = { .name = "BOILERS", .devices = DEVICES_BOILERS };
static const potd_t BOILER1 = { .name = "BOILER1", .devices = DEVICES_BOILER1 };
static const potd_t BOILER3 = { .name = "BOILER3", .devices = DEVICES_BOILER3 };

static struct tm now_tm, *now = &now_tm;
static int sock = 0;

// local dstate memory
static dstate_t dstate_seconds[60], dstate_current;

// global dstate pointer
dstate_t *dstate = &dstate_current;

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

static int ramp_heater(device_t *heater, int power) {
	if (!power || heater->state == Disabled || heater->state == Standby)
		return 0; // 0 - continue loop

	// heating disabled except override
	if (power > 0 && !GSTATE_HEATING && !heater->override) {
		power = 0;
		heater->state = Standby;
	}

	// keep on when already on
	if (power > 0 && heater->power)
		return 0;

	// not enough power available to switch on
	if (power > 0 && power < heater->total)
		return 0;

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

#if defined(TRON) || defined(ODROID)
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
static int ramp_boiler(device_t *boiler, int power) {
	if (!power || boiler->state == Disabled || boiler->state == Standby)
		return 0; // 0 - continue loop

	// cannot send UDP if we don't have an IP
	if (boiler->addr == NULL)
		return 0;

	// already full up
	if (boiler->power == 100 && power > 0)
		return 0;

	// already full down
	if (boiler->power == 0 && power < 0)
		return 0;

	// summer: charging boilers only between configured FROM / TO
	if (boiler->power == 0 && power > 0 && GSTATE_SUMMER && boiler->from && boiler->to && (now->tm_hour < boiler->from || now->tm_hour >= boiler->to)) {
		boiler->state = Standby;
		return 0;
	}

	// power steps
	int step = power * 100 / boiler->total;
	if (!step)
		return 0;

	// do single steps at warm up due to much smaller cold resistance
	if (boiler->power < 5 && 1 < step && step < 5)
		step = 1;

	// transform power into 0..100%
	power = boiler->power + step;
	CUT(power, 100);
	CUT_LOW(power, 0);

	// not enough to start up - electronic thermostat struggles at too less power
	if (boiler->power == 0 && boiler->min && power < boiler->min)
		return 0;

	// check if override is active
	power = check_override(boiler, power);

	// check if update is necessary
	if (boiler->power == power)
		return 0;

	// send UDP message to device
	char message[16];
	snprintf(message, 16, "p:%d:%d", power, 0);

	if (step < 0)
		xdebug("SOLAR ramp↓ %s step %d UDP %s", boiler->name, step, message);
	else
		xdebug("SOLAR ramp↑ %s step +%d UDP %s", boiler->name, step, message);

#if defined(TRON) || defined(ODROID)
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

	// electronic thermostat takes more time to react on startup
	int wait = boiler->power == 0 ? WAIT_RESPONSE * 2 : WAIT_RESPONSE;

	// update power values
	boiler->delta = (power - boiler->power) * boiler->total / 100;
	boiler->load = power * boiler->total / 100;
	boiler->power = power;
	store_meter_power(boiler);
	return wait; // loop done
}

static int ramp_akku(device_t *akku, int power) {
	// init - set to discharge when offline, otherwise to standby
	if (akku->power == -1)
		return PSTATE_OFFLINE ? akku_discharge(akku, 0) : akku_standby(akku);

	// ramp down request
	if (power < 0) {

		// consume ramp down up to current charging power
		akku->delta = power < pstate->akku ? pstate->akku : power;

		// akku ramps down itself when charging, otherwise forward to next device
		return pstate->akku < -NOISE ? 1 : 0;
	}

	// ramp up request
	if (power > 0) {

		// expect to consume all DC power up to battery's charge maximum
		int max = akku_charge_max();
		akku->delta = (pstate->mppt1 + pstate->mppt2) < max ? (pstate->mppt1 + pstate->mppt2) : max;

		// set into standby when full
		if (gstate->soc == 1000)
			return akku_standby(akku);

		// forward to next device if we have grid upload in despite of charging
		if (AKKU_CHARGING && PSTATE_GRID_ULOAD)
			return 0; // continue loop

		// start charging when flag is set
		if (DSTATE_CHARGE_AKKU) {
			int limit = GSTATE_SUMMER || gstate->today > akku_capacity() * 2 ? AKKU_LIMIT_CHARGE : 0;
			return akku_charge(akku, limit);
		}
	}

	return 0; // continue loop
}

static void create_dstate_json() {
	store_struct_json((int*) dstate, DSTATE_SIZE, DSTATE_HEADER, RUN SLASH DSTATE_JSON);
}

static void create_devices_json() {
	FILE *fp = fopen(RUN SLASH DEVICES_JSON, "wt");
	if (fp == NULL)
		return;

	int i = 0;
	fprintf(fp, "[");
	for (device_t **dd = potd->devices; *dd; dd++) {
		if (i++)
			fprintf(fp, ",");
		if (DD == AKKU)
			fprintf(fp, DSTATE_TEMPLATE, DD->name, DD->state, DD->power, akku_charge_max(), pstate->akku);
		else
			fprintf(fp, DSTATE_TEMPLATE, DD->name, DD->state, DD->power, DD->total, DD->load);
	}

	fprintf(fp, "]");
	fflush(fp);
	fclose(fp);
}

static void print_dstate(device_t *d) {
	char line[512], value[16]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "DSTATE");
	xlogl_bits16(line, "flags", dstate->flags);
	if (!PSTATE_OFFLINE) {
		xlogl_int_noise(line, NOISE, 0, "Ramp", dstate->ramp);
		xlogl_int(line, "XLoad", dstate->xload);
		xlogl_int(line, "DLoad", dstate->dload);
		strcat(line, "   ");
	}
	for (device_t **dd = potd->devices; *dd; dd++) {
		switch (DD->state) {
		case Disabled:
			snprintf(value, 5, " .");
			break;
		case Active:
			if (DD->adj)
				snprintf(value, 5, " %3d", DD->power);
			else
				snprintf(value, 5, " %c", DD->power ? 'x' : '_');
			break;
		case Active_Checked:
			if (DD->adj)
				snprintf(value, 6, " %3d!", DD->power);
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

	strcat(line, "   potd ");
	strcat(line, potd ? potd->name : "NULL");

	if (dstate->lock)
		xlogl_int(line, "   Lock", dstate->lock);
	xlogl_end(line, strlen(line), 0);
}

// call device specific ramp function
static int ramp_device(device_t *d, int power) {
	if (power < 0)
		xdebug("SOLAR ramp↓ %d %s", power, d->name);
	else if (power > 0)
		xdebug("SOLAR ramp↑ +%d %s", power, d->name);
	else
		xdebug("SOLAR ramp 0 %s", d->name);

	// set/reset response lock
	int ret = (d->ramp)(d, power);
	if (dstate->lock < ret)
		dstate->lock = ret;

	return ret;
}

static int select_program(const potd_t *p) {
	if (potd == p)
		return 0;

	// potd has changed - reset all devices (except AKKU) and set AKKU to initial state
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD != AKKU)
			ramp_device(DD, DOWN);
	if (AKKU_CHARGING)
		AKKU->power = -1;

	xlog("SOLAR selecting %s program of the day", p->name);
	potd = (potd_t*) p;
	dstate->lock = WAIT_RESPONSE;

	return 0;
}

// choose program of the day
static int choose_program() {
	// return select_program(&GREEDY);
	// return select_program(&MODEST);

	// summer
	if (GSTATE_SUMMER)
		return select_program(&PLENTY);

	// akku is empty - charging akku has priority
	if (gstate->soc < 100)
		return select_program(&MODEST);

	// we will NOT survive - charging akku has priority
	if (gstate->survive < 1000)
		return select_program(&MODEST);

	// survive but tomorrow not enough PV - charging akku has priority
	if (GSTATE_WINTER && gstate->tomorrow < akku_capacity())
		return select_program(&MODEST);

	// quota not yet reached and akku not yet enough to survive
	if (gstate->success < 1000 && gstate->akku < gstate->need_survive)
		return select_program(&MODEST);

	// start heating asap and charge akku tommorrow
	if (gstate->tomorrow > gstate->today)
		return select_program(&GREEDY);

	// survive but not enough for heating --> load boilers
	if (gstate->heating < 1000)
		return select_program(&BOILERS);

	// enough PV available to survive + heating
	return select_program(&PLENTY);
}

static void emergency() {
	akku_discharge(AKKU, 0); // enable discharge no limit
	for (device_t **dd = DEVICES; *dd; dd++)
		ramp_device(DD, DOWN);
	xlog("SOLAR emergency shutdown at akku=%d grid=%d ", pstate->akku, pstate->grid);
}

static void burnout() {
	akku_discharge(AKKU, 0); // enable discharge no limit
	solar_override_seconds("küche", WAIT_BURNOUT);
	solar_override_seconds("wozi", WAIT_BURNOUT);
	xlog("SOLAR burnout soc=%.1f temp=%.1f", FLOAT10(gstate->soc), FLOAT10(gstate->temp_in));
}

static int ramp_multi(device_t *d) {
	int ret = ramp_device(d, dstate->ramp);
	if (ret) {
		// recalculate ramp power
		int old_ramp = dstate->ramp;
		dstate->ramp -= d->delta + d->delta / 2; // add 50%
		// too less to forward
		if (old_ramp > 0 && dstate->ramp < NOISE * 2)
			dstate->ramp = 0;
		if (old_ramp < 0 && dstate->ramp > NOISE * -2)
			dstate->ramp = 0;
		if (dstate->ramp)
			msleep(33);
	}
	return ret;
}

static device_t* rampup() {
	if (DSTATE_ALL_UP || DSTATE_ALL_STANDBY)
		return 0;

	device_t *d = 0, **dd = potd->devices;
	while (*dd && dstate->ramp > 0) {
		if (ramp_multi(DD))
			d = DD;
		dd++;
	}

	return d;
}

static device_t* rampdown() {
	if (DSTATE_ALL_DOWN || DSTATE_ALL_STANDBY)
		return 0;

	// jump to last entry
	device_t *d = 0, **dd = potd->devices;
	while (*dd)
		dd++;

	// now go backward - this gives reverse order
	while (dd-- != potd->devices && dstate->ramp < 0)
		if (ramp_multi(DD))
			d = DD;

	return d;
}

static device_t* ramp() {
	if (!PSTATE_VALID || PSTATE_OFFLINE)
		return 0;

	// calculate the power for ramping
	dstate->ramp = pstate->grid * -1;

	// stable when grid between -RAMP_WINDOW..+NOISE
	if (-RAMP_WINDOW < pstate->grid && pstate->grid <= NOISE)
		dstate->ramp = 0;

	// ramp down when akku is discharging
	if (pstate->akku > NOISE)
		dstate->ramp -= pstate->akku;

	// ramp down between NOISE and RAMP_WINDOW
	if (NOISE < pstate->grid && pstate->grid <= RAMP_WINDOW)
		dstate->ramp = -RAMP_WINDOW;

	// when akku is charging it regulates around 0, so set stable window between -RAMP_WINDOW..+RAMP_WINDOW
	if (pstate->akku < -NOISE && -RAMP_WINDOW < pstate->grid && pstate->grid <= RAMP_WINDOW)
		dstate->ramp = 0;

// TODO wie auf m1 zugreifen?
//	// 50% more ramp down when PV tendency is falling
//	if (dstate->ramp < 0 && m1->dpv < 0)
//		dstate->ramp += dstate->ramp / 2;
//
//	// delay ramp up as long as average PV is below average load
//	int m1load = (m1->load + m1->load / 10) * -1; // + 10%;
//	if (dstate->ramp > 0 && m1->pv < m1load) {
//		xdebug("SOLAR delay ramp up as long as average pv %d < average load %d", m1->pv, m1load);
//		dstate->ramp = 0;
//	}

	// delay small ramp up when we just had akku discharge or grid download
	if (0 < dstate->ramp && dstate->ramp < MINIMUM) {
		if (PSTATE_AKKU_DCHARGE || PSTATE_GRID_DLOAD) {
			xdebug("SOLAR delay ramp up %d < %d due to %s %s", dstate->ramp, MINIMUM, PSTATE_AKKU_DCHARGE ? "discharge" : "", PSTATE_GRID_DLOAD ? "gridload" : "");
			dstate->ramp = 0;
		}
	}

	// delay ramp up when unstable or distortion unless we have enough power
	if (0 < dstate->ramp && dstate->ramp < ENOUGH)
		if (!PSTATE_STABLE || PSTATE_DISTORTION) {
			xdebug("SOLAR delay ramp up %d < %d due to %s %s", dstate->ramp, ENOUGH, !PSTATE_STABLE ? "unstable" : "", PSTATE_DISTORTION ? "distortion" : "");
			dstate->ramp = 0;
		}

	// ramp down in reverse order
	if (dstate->ramp < 0)
		return rampdown();

	// ramp up in order
	if (dstate->ramp > 0)
		return rampup();

	return 0;
}

static device_t* steal() {
	if (!PSTATE_VALID || PSTATE_OFFLINE || !PSTATE_STABLE || PSTATE_DISTORTION || DSTATE_ALL_UP || DSTATE_ALL_DOWN || DSTATE_ALL_STANDBY)
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
		if (ramp_device(DD, p)) {
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
	ramp_device(d, power);
	return d;
}

static device_t* standby() {
	if (!PSTATE_VALID || PSTATE_OFFLINE || !PSTATE_STABLE || PSTATE_DISTORTION || DSTATE_ALL_STANDBY || !DSTATE_CHECK_STANDBY || pstate->pv < BASELOAD * 2)
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
	if (!PSTATE_VALID || PSTATE_OFFLINE)
		return 0;

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
	if (r)
		xdebug("SOLAR response detected at phase %s %s %s", r1 ? "1" : "", r2 ? "2" : "", r3 ? "3" : "");

	// load is completely satisfied from secondary inverter
	int extra = pstate->ac2 > pstate->load * -1;

	// wait more to give akku time to release power when ramped up
	int wait = AKKU_CHARGING && delta > 0 && !extra ? 3 * WAIT_RESPONSE : 0;

	// response OK
	if (r && (d->state == Active || d->state == Active_Checked)) {
		xdebug("SOLAR response OK from %s, delta expected %d actual %d %d %d", d->name, delta, d1, d2, d3);
		d->noresponse = 0;
		dstate->lock = wait;
		return dstate->lock ? d : 0;
	}

	// standby check was negative - we got a response
	if (d->state == Standby_Check && r) {
		xdebug("SOLAR standby check negative for %s, delta expected %d actual %d %d %d", d->name, delta, d1, d2, d3);
		d->noresponse = 0;
		d->state = Active_Checked; // mark Active with standby check performed
		dstate->lock = wait;
		return d; // recalculate in next round
	}

	// standby check was positive -> set device into standby
	if (d->state == Standby_Check && !r) {
		xdebug("SOLAR standby check positive for %s, delta expected %d actual %d %d %d  --> entering standby", d->name, delta, d1, d2, d3);
		ramp_device(d, d->total * -1);
		d->noresponse = d->delta = dstate->lock = 0; // no response from switch off expected
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

static void calculate_dstate() {
	// clear state flags and values
	dstate->flags = dstate->xload = dstate->dload = 0;

	// get history states
	dstate_t *s1 = DSTATE_LAST1;
	dstate_t *s2 = DSTATE_LAST2;

	dstate->flags |= FLAG_ALL_UP | FLAG_ALL_DOWN | FLAG_ALL_STANDBY;
	for (device_t **dd = DEVICES; *dd; dd++) {
		// calculated load
		dstate->xload += DD->load;

		// flags for all devices up/down/standby
		// (!) power can be -1 when uninitialized
		if (DD->power > 0)
			dstate->flags &= ~FLAG_ALL_DOWN;
		if (!DD->power || (DD->adj && DD->power != 100))
			dstate->flags &= ~FLAG_ALL_UP;
		if (DD->state != Standby)
			dstate->flags &= ~FLAG_ALL_STANDBY;
	}

	// delta between actual load an calculated load
	int p_load = -1 * pstate->load; // - BASELOAD;
	dstate->dload = p_load > 0 && dstate->xload ? p_load * 100 / dstate->xload : 0;

	// indicate standby check when actual load is 3x below 50% of calculated load
	if (dstate->xload && dstate->dload < 50 && s1->dload < 50 && s2->dload < 50) {
		dstate->flags |= FLAG_CHECK_STANDBY;
		xdebug("SOLAR set FLAG_CHECK_STANDBY load=%d xload=%d dxload=%d", pstate->load, dstate->xload, dstate->dload);
	}

	// check if we we need to charge the akku
	if (GSTATE_WINTER)
		// winter: always
		dstate->flags |= FLAG_CHARGE_AKKU;
	else if (GSTATE_SUMMER) {
		// summer: charging between 9 and 15 o'clock when below 20%
		if (gstate->soc < 200 && now->tm_hour >= 9 && now->tm_hour < 15)
			dstate->flags |= FLAG_CHARGE_AKKU;
	} else {
		// autumn/spring: charging between 9 and 15 o'clock when below 50% or tomorrow not enough pv
		if (gstate->soc < 500 && now->tm_hour >= 9 && now->tm_hour < 15)
			dstate->flags |= FLAG_CHARGE_AKKU;
		if (gstate->tomorrow < akku_capacity() * 2)
			dstate->flags |= FLAG_CHARGE_AKKU;
	}

	// copy to history
	memcpy(DSTATE_NOW, (void*) dstate, sizeof(dstate_t));
}

static void daily() {
	xdebug("SOLAR dispatcher executing daily tasks...");
}

static void hourly() {
	xdebug("SOLAR dispatcher executing hourly tasks...");

	for (device_t **dd = DEVICES; *dd; dd++) {
		// reset noresponse counters
		DD->noresponse = 0;

		// set all devices back to active
		if (DD->state == Standby || DD->state == Active_Checked)
			if (DD != AKKU)
				DD->state = Active;

		// force off when offline
		if (PSTATE_OFFLINE)
			ramp_device(DD, DOWN);
	}
}

static void minly() {
	if (PSTATE_OFFLINE)
		dstate->ramp = 0;

	// set akku to DISCHARGE if we have long term grid download
	if (PSTATE_GRID_DLOAD) {
		int tiny_tomorrow = gstate->tomorrow < akku_capacity();

		// winter: limit discharge and try to extend ttl as much as possible
		int limit = GSTATE_WINTER && (gstate->survive < 0 || tiny_tomorrow) ? AKKU_LIMIT_DISCHARGE : 0;
		akku_discharge(AKKU, limit);

		// minimum SOC: standard 5%, winter and tomorrow not much PV expected 10%
		int min_soc = GSTATE_WINTER && tiny_tomorrow && gstate->soc > 111 ? 10 : 5;
		akku_set_min_soc(min_soc);
	}
}

int solar_override_seconds(const char *name, int seconds) {
	device_t *d = get_by_name(name);
	if (!d)
		return 0;
	if (d->override)
		return 0;

	xlog("SOLAR Activating Override on %s", d->name);
	d->power = -1;
	if (!d->id)
		d->addr = resolve_ip(d->name);
	if (d->adj && d->addr == 0)
		d->state = Disabled; // disable when we don't have an ip address to send UDP messages

	d->state = Active;
	d->override = time(NULL) + seconds;
	ramp_device(d, d->total);

	return 0;
}

int solar_override(const char *name) {
	return solar_override_seconds(name, OVERRIDE);
}

static void loop() {
	time_t now_ts;
	device_t *device = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// the SOLAR main loop
	while (1) {

		// get actual time and store global
		now_ts = time(NULL);
		localtime_r(&now_ts, &now_tm);

		// emergency mode
		if (PSTATE_EMERGENCY)
			emergency();

		// burnout mode
		if (PSTATE_BURNOUT)
			burnout();

		// no actions until lock is expired
		if (dstate->lock)
			dstate->lock--;

		else {

			// prio1: check response from previous action
			if (device)
				device = response(device);

			// prio2: perform standby check logic
			if (!device)
				device = standby();

			// prio3: ramp
			if (!device)
				device = ramp();

			// prio4: check if higher prioritized device can steal from lower prioritized
			if (!device)
				device = steal();

		}
		// calculate device state
		calculate_dstate();

		// print dstate once per minute / on device action
		if (MINLY || device)
			print_dstate(device);

		// cron jobs
		if (MINLY) {
			minly();

			if (HOURLY) {
				hourly();

				if (DAILY)
					daily();
			}
		}

		// web output
		create_dstate_json();
		create_devices_json();

		// wait for next second
		while (now_ts == time(NULL))
			msleep(333);
	}
}

static int init() {
	// initialize global time structure
	time_t now_ts = time(NULL);
	localtime_r(&now_ts, &now_tm);

	// create a socket for sending UDP messages
	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == 0)
		return xerr("Error creating socket");

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

	choose_program();

	return 0;
}

static void stop() {
	if (sock)
		close(sock);
}

MCP_REGISTER(solar_dispatcher, 11, &init, &stop, &loop);
