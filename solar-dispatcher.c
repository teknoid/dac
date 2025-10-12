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
#include "sunspec.h"
#include "utils.h"
#include "mcp.h"

// JSON files for webui
#define DSTATE_JSON				"dstate.json"
#define DEVICES_JSON			"devices.json"

#define AKKU_STANDBY			(AKKU->state == Standby)
#define AKKU_CHARGING			(AKKU->state == Charge    && AKKU->load >  NOISE)
#define AKKU_DISCHARGING		(AKKU->state == Discharge && AKKU->load < -NOISE)
#define AKKU_LIMIT_CHARGE3X		1750
#define AKKU_LIMIT_CHARGE2X		2500
#define AKKU_LIMIT_DISCHARGE	BASELOAD

#define OVERRIDE				600
#define STANDBY_NORESPONSE		5

#define OVERLOAD_STANDBY_FORCE	1000
#define OVERLOAD_STANDBY		150
#define OVERLOAD				110

#define WAIT_RESPONSE			6
#define WAIT_THERMOSTAT			12
#define WAIT_AKKU				15
#define WAIT_START_CHARGE		30

// dstate access pointers
#define DSTATE_NOW				(&dstate_seconds[now->tm_sec])
#define DSTATE_LAST5			(&dstate_seconds[now->tm_sec > 4 ? now->tm_sec -  5 : (now->tm_sec -  5 + 60)])
#define DSTATE_LAST10			(&dstate_seconds[now->tm_sec > 9 ? now->tm_sec - 10 : (now->tm_sec - 10 + 60)])

#define DD						(*dd)

// program of the day - choosen by mosmix forecast data
typedef struct potd_t {
	const char *name;
	device_t **devices;
} potd_t;
static potd_t *potd = 0;

// device ramp function signatures
static void ramp_heater(device_t *device);
static void ramp_boiler(device_t *device);
static void ramp_akku(device_t *device);

// devices
static device_t a1 = { .name = "akku", .total = 0, .rf = &ramp_akku, .adj = 0 }, *AKKU = &a1;
static device_t b1 = { .name = "boiler1", .id = BOILER1, .total = 2000, .rf = &ramp_boiler, .adj = 1, .r = 0 };
static device_t b2 = { .name = "boiler2", .id = BOILER2, .total = 2000, .rf = &ramp_boiler, .adj = 1, .r = 0 };
static device_t b3 = { .name = "boiler3", .id = BOILER3, .total = 2000, .rf = &ramp_boiler, .adj = 1, .r = 0, .from = 10, .to = 15, .min = 100 };
static device_t h1 = { .name = "küche", .id = SWITCHBOX, .total = 500, .rf = &ramp_heater, .adj = 0, .r = 1, .host = "switchbox" };
static device_t h2 = { .name = "wozi", .id = SWITCHBOX, .total = 500, .rf = &ramp_heater, .adj = 0, .r = 2, .host = "switchbox" };
static device_t h3 = { .name = "schlaf", .id = PLUG5, .total = 500, .rf = &ramp_heater, .adj = 0, .r = 0, .host = "plug5" };
static device_t h4 = { .name = "tisch", .id = SWITCHBOX, .total = 200, .rf = &ramp_heater, .adj = 0, .r = 3, .host = "switchbox" };

// all devices, needed for initialization
static device_t *DEVICES[] = { &a1, &b1, &b2, &b3, &h1, &h2, &h3, &h4, 0 };

// first charge akku, then boilers, then heaters
static device_t *DEVICES_MODEST[] = { &a1, &b1, &h1, &h2, &h3, &h4, &b2, &b3, 0 };

// steal all akku charge power
static device_t *DEVICES_GREEDY[] = { &h1, &h2, &h3, &h4, &b1, &b2, &b3, &a1, 0 };

// heaters, then akku, then boilers (catch remaining pv from secondary inverters or if akku is not able to consume all generated power)
static device_t *DEVICES_PLENTY[] = { &h1, &h2, &h3, &h4, &a1, &b1, &b2, &b3, 0 };

// force boiler heating first
//static device_t *DEVICES_BOILERS[] = { &b1, &b2, &b3, &h1, &h2, &h3, &h4, &a1, 0 };
//static device_t *DEVICES_BOILER1[] = { &b1, &a1, &b2, &b3, &h1, &h2, &h3, &h4, 0 };
//static device_t *DEVICES_BOILER3[] = { &b3, &a1, &b1, &b2, &h1, &h2, &h3, &h4, 0 };

// define POTDs
static const potd_t MODEST = { .name = "MODEST", .devices = DEVICES_MODEST };
static const potd_t GREEDY = { .name = "GREEDY", .devices = DEVICES_GREEDY };
static const potd_t PLENTY = { .name = "PLENTY", .devices = DEVICES_PLENTY };
//static const potd_t BOILERS = { .name = "BOILERS", .devices = DEVICES_BOILERS };
//static const potd_t BOILER1 = { .name = "BOILER1", .devices = DEVICES_BOILER1 };
//static const potd_t BOILER3 = { .name = "BOILER3", .devices = DEVICES_BOILER3 };

static struct tm now_tm, *now = &now_tm;
static int sock = 0;

// local dstate memory
static dstate_t dstate_seconds[60], dstate_current;

// current ramped device
static device_t *device = 0;

// global dstate pointer
dstate_t *dstate = &dstate_current;

static device_t* get_by_name(const char *name) {
	for (device_t **dd = DEVICES; *dd; dd++)
		if (!strcmp(DD->name, name))
			return DD;
	return 0;
}

static device_t* get_by_id(unsigned int id, int relay) {
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD->id == id && DD->r == relay)
			return DD;
	return 0;
}

static void ramp_heater(device_t *heater) {
	int power = heater->ramp;
	heater->ramp = 0;

	if (!power || heater->state == Disabled || heater->state == Initial || heater->state == Standby)
		return;

	// heating disabled
	if (heater->state == Auto && power > 0 && !GSTATE_HEATING) {
		power = 0;
		heater->state = Standby;
	}

	// keep on when already on
	if (power > 0 && heater->power)
		return;

	// not enough power available to switch on
	if (power > 0 && power < heater->total)
		return;

	// transform power into on/off
	power = power > 0 ? 1 : 0;

	// check if update is necessary
	if (heater->power == power)
		return;

	xdebug("SOLAR switching %s %s", heater->name, power ? "ON" : "OFF");

#ifndef SOLAR_MAIN
	tasmota_power(heater->id, heater->r, power);
#endif

	// update power values
	dstate->flags |= FLAG_ACTION;
	dstate->lock = WAIT_RESPONSE; // TODO abhängig von ramp power, je größer desto länger
	heater->power = power;
	heater->ramp = power ? heater->total : heater->total * -1;
	heater->load = power ? heater->total : 0;

	// store phase power to detect response
	dstate->p1 = pstate->p1;
	dstate->p2 = pstate->p2;
	dstate->p3 = pstate->p3;
	device = heater;

	return;
}

// echo p:0:0 | socat - udp:boiler3:1975
// for i in `seq 1 10`; do let j=$i*10; echo p:$j:0 | socat - udp:boiler1:1975; sleep 1; done
static void ramp_boiler(device_t *boiler) {
	int power = boiler->ramp;
	boiler->ramp = 0;

	if (!power || boiler->state == Disabled || boiler->state == Initial || boiler->state == Standby)
		return;

	// cannot send UDP if we don't have an IP
	if (boiler->addr == NULL)
		return;

	// already full up
	if (boiler->power == 100 && power > 0)
		return;

	// already full down
	if (boiler->power == 0 && power < 0)
		return;

	// charging boilers only between configured FROM / TO (winter always)
	if (boiler->power == 0 && power > 0 && !GSTATE_WINTER && boiler->from && boiler->to && (now->tm_hour < boiler->from || now->tm_hour >= boiler->to)) {
		boiler->state = Standby;
		return;
	}

	// not enough to start up - electronic thermostat struggles at too less power
	if (boiler->state == Auto && boiler->power == 0 && power < boiler->min)
		return;

	// power steps
	int step = power * 100 / boiler->total;
	if (power < 0 && (power < step * boiler->total / 100))
		step -= 1; // one step more when not enough
	if (!step)
		return;

	// do single steps at warm up due to much smaller cold resistance
	if (boiler->power < 5 && 1 < step && step < 5)
		step = 1;

	// transform power into 0..100%
	power = boiler->power + step;

	// electronic thermostat - leave boiler alive when in AUTO mode
	int min = boiler->state == Auto && boiler->min && !DEV_FORCE_OFF(boiler) && !GSTATE_OFFLINE ? boiler->min * 100 / boiler->total : 0;

	HICUT(power, 100);
	LOCUT(power, min);

	// check if update is necessary
	if (boiler->power == power)
		return;

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
	if (ret < 0) {
		xerr(0, "Sendto failed on %s %s", boiler->addr, strerror(ret));
		return;
	}
#endif

	// update power values
	dstate->flags |= FLAG_ACTION;
	dstate->lock = boiler->power == 0 ? WAIT_THERMOSTAT : WAIT_RESPONSE; // electronic thermostat takes more time at startup // TODO abhängig von ramp power, je größer desto länger
	boiler->power = power;
	boiler->ramp = step * boiler->total / 100;
	boiler->load = power * boiler->total / 100;

	// store phase power to detect response
	dstate->p1 = pstate->p1;
	dstate->p2 = pstate->p2;
	dstate->p3 = pstate->p3;
	device = boiler;

	return;
}

static void ramp_akku(device_t *akku) {
	int power = akku->ramp;
	akku->ramp = 0;

	// akku ramps up and down itself - emulating ramp behavior

	// ramp down request
	if (power < 0) {

		// leave akku a little bit charging, otherwise up to it's current charging power
		if (AKKU_CHARGING)
			akku->ramp = (akku->load < MINIMUM) || (power * -1 < akku->load) ? power : akku->load * -1;

	}

	// ramp up request
	if (power > 0) {

		// set into standby when full
		if (gstate->soc == 1000) {
			if (!akku_standby(akku))
				dstate->flags |= FLAG_ACTION;
			return;
		}

		// aku is charging but we still have grid upload - either on limited akku charging or extra power
		if (akku->state == Charge && GSTATE_GRID_ULOAD)
			return;

		// akku is really charging
		if (AKKU->state == Charge && AKKU->load > NOISE) {
			if (akku->load < MINIMUM) {
				// leave akku a little bit charging to avoid grid load
				akku->ramp = MINIMUM;
			} else {
				// all available DC power up to either charge maximum or charge limit
				akku->ramp = pstate->mppt1 + pstate->mppt2 - akku->load;
				HICUT(power, akku->total)
			}
			return;
		}

		// start charging
		// TODO limit basierend auf pvmin/pvmax/pvavg setzen
		if (GSTATE_CHARGE_AKKU) {
			int limit = 0;
			if (GSTATE_SUMMER || gstate->today > params->akku_capacity * 2)
				limit = AKKU_LIMIT_CHARGE2X;
			if (GSTATE_SUMMER || gstate->today > params->akku_capacity * 3)
				limit = AKKU_LIMIT_CHARGE3X;
			if (!akku_charge(akku, limit)) {
				dstate->flags |= FLAG_ACTION;
				dstate->lock = WAIT_START_CHARGE;
			}
		}
	}
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
#define DEVICE_TEMPLATE	"{\"id\":\"%06X\", \"r\":\"%d\", \"name\":\"%s\", \"host\":\"%s\", \"state\":%d, \"power\":%d, \"flags\":%d, \"total\":%d, \"load\":%d, \"steal\":%d}"
		fprintf(fp, DEVICE_TEMPLATE, DD->id, DD->r, DD->name, DD->host, DD->state, DD->power, DD->flags, DD->total, DD->load, DD->steal);
	}
	fprintf(fp, "]");

	fflush(fp);
	fclose(fp);
}

static void print_dstate() {
	char line[512], value[6]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "DSTATE ");
	xlogl_bits16(line, NULL, dstate->flags);
	if (!GSTATE_OFFLINE) {
		xlogl_int(line, "CLoad", dstate->cload);
		xlogl_int(line, "RLoad", dstate->rload);
		strcat(line, "   ");
	}
	for (device_t **dd = potd->devices; *dd; dd++) {
		switch (DD->state) {
		case Disabled:
			snprintf(value, 6, " .");
			break;
		case Initial:
			snprintf(value, 6, " i");
			break;
		case Standby:
			snprintf(value, 6, " S");
			break;
		case Manual:
			snprintf(value, 6, " %c", DD->power ? 'M' : 'm');
			break;
		case Auto:
			if (DD->adj) {
				if (DEV_RESPONSE(DD))
					snprintf(value, 6, " %3d!", DD->power);
				else
					snprintf(value, 6, " %3d", DD->power);
			} else {
				if (DEV_RESPONSE(DD))
					snprintf(value, 6, " %c", DD->power ? 'X' : '_');
				else
					snprintf(value, 6, " %c", DD->power ? 'x' : '_');
			}
			break;
		case Charge:
			snprintf(value, 6, " C");
			break;
		case Discharge:
			snprintf(value, 6, " D");
			break;
		default:
			snprintf(value, 6, " ?");
			break;
		}
		strcat(line, value);
	}

	strcat(line, "   potd ");
	strcat(line, potd ? potd->name : "NULL");

	if (dstate->lock)
		xlogl_int(line, "   Lock", dstate->lock);

	xlogl_end(line, strlen(line), 0);
}

static void toggle_device(device_t *d) {
	xlog("SOLAR toggle id=%06X relay=%d power=%d load=%d name=%s", d->id, d->r, d->power, d->load, d->name);
	d->ramp = !d->power ? d->total : d->total * -1;
	(d->rf)(d);
}

// call device specific ramp function
static void ramp_device(device_t *d, int power) {
	if (d->state == Manual)
		return;

	d->ramp = power;
	(d->rf)(d);
}

static int select_program(const potd_t *p) {
	if (potd == p)
		return 0; // no change

	// potd has changed - set AKKU to standby when charging
	if (potd != 0 && AKKU_CHARGING)
		akku_standby(AKKU);

	xlog("SOLAR selecting %s program of the day", p->name);
	potd = (potd_t*) p;
	dstate->lock = WAIT_RESPONSE;

	return 0;
}

// choose program of the day
static int choose_program() {
	// return select_program(&GREEDY);
	// return select_program(&MODEST);

	// summer or enough pv
	if (GSTATE_SUMMER || gstate->today > 50000)
		return select_program(&PLENTY);

	// akku is empty - charging akku has priority
	if (gstate->soc < 100)
		return select_program(&MODEST);

	// we will NOT survive - charging akku has priority
	if (gstate->survive < 1000)
		return select_program(&MODEST);

	// survive but tomorrow not enough PV - charging akku has priority
	if (GSTATE_WINTER && gstate->tomorrow < params->akku_capacity)
		return select_program(&MODEST);

	// forecast below 50% and akku not yet enough to survive
	if (gstate->forecast < 500 && AKKU_AVAILABLE < gstate->nsurvive)
		return select_program(&MODEST);

	// start heating asap and charge akku tommorrow
	if (gstate->tomorrow > gstate->today)
		return select_program(&GREEDY);

	// enough PV available to survive + heating
	return select_program(&PLENTY);
}

static void emergency() {
	xlog("SOLAR emergency shutdown");
	akku_discharge(AKKU, 0); // enable discharge no limit
	for (device_t **dd = DEVICES; *dd; dd++)
		ramp_device(DD, DD->total * -1);
}

// ramp up in POTD order
static void rampup() {
	device_t **dd = potd->devices;
	while (*dd && dstate->ramp >= RAMP) {
		int oldramp = dstate->ramp;
		ramp_device(DD, dstate->ramp);
		dstate->ramp -= DD->ramp;
		xlog("SOLAR ramp↑ %3d --> %s=%3d --> %3d", oldramp, DD->name, DD->ramp, dstate->ramp);
		if (DD->ramp)
			msleep(111);
		dd++;
	}
}

// ramp down inverse POTD order
static void rampdown() {
	// jump to last entry
	device_t **dd = potd->devices;
	while (*dd)
		dd++;

	// now go backward - this gives reverse order
	while (dd-- != potd->devices && dstate->ramp <= -RAMP) {
		int oldramp = dstate->ramp;
		ramp_device(DD, dstate->ramp);
		dstate->ramp -= DD->ramp;
		xlog("SOLAR ramp↓ %3d <-- %s=%3d <-- %3d", dstate->ramp, DD->name, DD->ramp, oldramp);
		if (DD->ramp)
			msleep(111);
	}
}

static void ramp() {
	dstate->ramp = dstate->surp;

	if (dstate->ramp <= -RAMP && !DSTATE_ALL_DOWN)
		rampdown();

	// allow rampup after rampdown if power was released

	if (dstate->ramp >= RAMP && !DSTATE_ALL_UP)
		rampup();
}

static device_t* perform_standby(device_t *d) {
	int power = d->adj ? (d->power < 50 ? +500 : -500) : (d->power ? d->total * -1 : d->total);
	xlog("SOLAR starting standby check on %s with power=%d", d->name, power);
	d->flags |= FLAG_STANDBY_CHECK; // set check flag
	ramp_device(d, power);
	return d;
}

static device_t* standby() {
	// try first powered adjustable device without RESPONSE flag
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD->state == Auto && !DEV_STANDBY_CHECKED(DD) && DD->power && DD->adj && !DEV_RESPONSE(DD))
			return perform_standby(DD);

	// try first powered adjustable device
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD->state == Auto && !DEV_STANDBY_CHECKED(DD) && DD->power && DD->adj)
			return perform_standby(DD);

	// try first powered dumb device without RESPONSE flag
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD->state == Auto && !DEV_STANDBY_CHECKED(DD) && DD->power && !DEV_RESPONSE(DD))
			return perform_standby(DD);

	// try first powered dumb device
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD->state == Auto && !DEV_STANDBY_CHECKED(DD) && DD->power)
			return perform_standby(DD);

	return 0;
}

static void steal() {
	// calculate steal power for any device in AUTO mode
	for (device_t **dd = DEVICES; *dd; dd++) {
		DD->steal = 0;
		if (DD == AKKU) {
			// steal max 75% of charging power
			DD->steal = DD->load * 0.75;
			dstate->steal += DD->steal;
			continue;
		}
		// only when in AUTO mode and no OVERLOAD (we cannot be sure if the power is really consumed)
		if (DD->state == Auto && dstate->rload < OVERLOAD) {
			if (DD->min)
				DD->steal = DD->load > DD->min ? DD->load - DD->min : 0; // all above minimum
			else
				DD->steal = DD->load; // all
			dstate->steal += DD->steal;
		}
	}

	// nothing to steal
	if (dstate->steal < RAMP)
		return;

	// check if we can steal from lower prioritized devices
	for (device_t **tt = potd->devices; *tt; tt++) {
		device_t *t = *tt; // thief

		//  when inverter produces ac output and akku is charging and not saturated (limited or maximum charge power reached)
		if (t == AKKU && pstate->ac1 > RAMP && AKKU_CHARGING && AKKU->power < 95) {
			// akku can not steal more than inverters ac output
			if (pstate->ac1 < dstate->steal)
				dstate->steal = pstate->ac1;

			// jump to last entry
			device_t **vv = potd->devices;
			while (*vv)
				vv++;

			// ramp down victims in inverse order
			int to_steal = dstate->steal;
			while (--vv != tt && to_steal > 0) {
				device_t *v = *vv;
				ramp_device(v, to_steal * -1);
				int given = v->ramp * -1;
				xlog("SOLAR AKKU steal %d/%d from %s", given, to_steal, v->name);
				to_steal -= given;
			}
			return;
		}

		// only thiefs in AUTO mode can steal
		if (t->state != Auto)
			continue;

		// thief already (full) on
		if (t->power == (t->adj ? 100 : 1))
			continue;

		// collect steal power from victims
		dstate->steal = 0;
		for (device_t **vv = tt + 1; *vv; vv++)
			dstate->steal += (*vv)->steal;

		// nothing to steal
		if (dstate->steal < RAMP)
			continue;

		// minimum power to ramp up thief: adjustable = 5% of total, dumb = total +10%
		int min = t->adj ? (t->total / 20) : (t->total * 1.1);

		// not enough to ramp up
		// TODO und die letzte minute lang genug da und stabil
		int total = dstate->steal + dstate->ramp;
		if (total < min)
			continue;

		// jump to last entry
		device_t **vv = potd->devices;
		while (*vv)
			vv++;

		// ramp down victims in inverse order till we have enough to ramp up thief
		int to_steal = min;
		while (--vv != tt && to_steal > 0) {
			device_t *v = *vv;
			ramp_device(v, to_steal * -1);
			int given = v->ramp * -1;
			xlog("SOLAR %s steal %d/%d from %s min=%d ramp=%d", t->name, given, to_steal, v->name, min, total);
			to_steal -= given;
		}

		// ramp up thief
		ramp_device(t, total);
		dstate->lock = AKKU_CHARGING ? WAIT_AKKU : WAIT_RESPONSE;
		device = 0; // expect no response when power is transferred from one to another
		return;
	}
}

static void response() {
	if (!device)
		return; // no response expected

	// valid response is at least 2/3 of last ramp
	int delta = device->ramp - device->ramp / 3;

	// check if we got a response on any phase
	int d1 = pstate->p1 - dstate->p1;
	int d2 = pstate->p2 - dstate->p2;
	int d3 = pstate->p3 - dstate->p3;
	int l1 = delta > 0 ? d1 > delta : d1 < delta;
	int l2 = delta > 0 ? d2 > delta : d2 < delta;
	int l3 = delta > 0 ? d3 > delta : d3 < delta;

	// is the device currently in standby check?
	int standby_check = DEV_STANDBY_CHECK(device);

	// response OK
	if (l1 || l2 || l3) {
		if (standby_check) {
			xlog("SOLAR %s standby check negative, delta expected %d actual %d %d %d lock=%d", device->name, delta, d1, d2, d3, dstate->lock);
			device->flags &= ~FLAG_STANDBY_CHECK; // remove check flag
			device->flags |= FLAG_STANDBY_CHECKED; // do not repeat the check
		} else
			xlog("SOLAR %s response ok at %s%s%s, delta %d %d %d exp %d lock=%d", device->name, l1 ? "L1" : "", l2 ? "L2" : "", l3 ? "L3" : "", d1, d2, d3, delta, dstate->lock);

		// wait more to give akku time to release power when ramped up or consume when stolen
		dstate->lock = AKKU_CHARGING ? WAIT_AKKU : 0;
		device->flags |= FLAG_RESPONSE; // flag with response OK
		device = 0;
		return;
	}

	// still awaiting response
	if (dstate->lock > 0)
		return;

	// no response during lock
	if (standby_check) {
		xlog("SOLAR standby check positive for %s, delta expected %d actual %d %d %d  --> entering standby", device->name, delta, d1, d2, d3);
		device->flags |= FLAG_FORCE_OFF;
		ramp_device(device, device->total * -1);
		device->state = Standby;
	} else
		xlog("SOLAR no response from %s", device->name);

	// remove flag when ramped up, otherwise ignore
	if (delta > 0)
		device->flags &= ~FLAG_RESPONSE;

	device = 0; // next action
}

static void calculate_dstate(time_t ts) {
	// clear values when offline
	if (GSTATE_OFFLINE) {
		dstate->surp = dstate->ramp = dstate->steal = dstate->flags = dstate->cload = dstate->rload = 0;
		return;
	}

	// take over surplus
	dstate->surp = pstate->surp;

	// clear state flags and values
	dstate->flags = dstate->cload = dstate->rload = 0;

	// update akku
	AKKU->load = pstate->batt * -1;
	AKKU->power = AKKU->total ? AKKU->load * 100 / AKKU->total : 0; // saturation -100%..0..100%

	dstate->flags |= FLAG_ALL_UP | FLAG_ALL_DOWN | FLAG_ALL_STANDBY;
	for (device_t **dd = DEVICES; *dd; dd++) {

		// akku has own logic above, check only devices in AUTO or MANUAL mode
		int check = DD != AKKU && (DD->state == Auto || DD->state == Manual);
		if (!check)
			continue;

		// calculated load
		dstate->cload += DD->load;

		// flags for all devices up/down/standby
		if (DD->power)
			dstate->flags &= ~FLAG_ALL_DOWN;
		if (!DD->power || (DD->adj && DD->power != 100))
			dstate->flags &= ~FLAG_ALL_UP;
		if (DD->state != Standby)
			dstate->flags &= ~FLAG_ALL_STANDBY;
	}

	// add load (max baseload) when devices active
	if (!DSTATE_ALL_DOWN)
		dstate->cload += pstate->load < params->baseload ? pstate->load : params->baseload;

	// ratio between calculated load and actual load
	dstate->rload = pstate->load ? dstate->cload * 100 / pstate->load : 0;

	// copy to history
	memcpy(DSTATE_NOW, (void*) dstate, sizeof(dstate_t));

	// no further actions
	if (!PSTATE_VALID || PSTATE_EMERGENCY || GSTATE_OFFLINE || DSTATE_ALL_STANDBY || device || dstate->lock)
		return;

	// ramp logic each 10 seconds (0, 10, 20, ...)
	// TODO oder wenn surplus < -MINUMUM irgendsowas...
	if (ts % 10 == 0)
		dstate->flags |= FLAG_ACTION_RAMP;

	// no further actions
	if (DSTATE_ALL_DOWN || pstate->pload < 120)
		return;

	// standby logic each 10 seconds (1, 11, 21, ...) on permanent OVERLOAD_STANDBY
	int overload = dstate->rload > OVERLOAD_STANDBY && DSTATE_LAST5->rload > OVERLOAD_STANDBY && DSTATE_LAST10->rload > OVERLOAD_STANDBY;
	if (ts % 10 == 1 && overload)
		dstate->flags |= FLAG_ACTION_STANDBY;

	// steal logic each 10 seconds (2, 12, 22, ...)
	if (ts % 10 == 2 && !overload)
		dstate->flags |= FLAG_ACTION_STEAL;
}

static void daily() {
	xdebug("SOLAR dispatcher executing daily tasks...");
}

static void hourly() {
	xdebug("SOLAR dispatcher executing hourly tasks...");

	// set all devices back to automatic and clear flags
	for (device_t **dd = DEVICES; *dd; dd++) {
		DD->flags = 0;
		if (DD->state == Manual || DD->state == Standby)
			DD->state = Auto;
		// force off when offline
		if (GSTATE_OFFLINE)
			ramp_device(DD, DD->total * -1);
	}
}

static void minly() {
	// update akku state
	akku_state(AKKU);

	// choose potd
	choose_program();

	if (GSTATE_BURNOUT) {
		xlog("SOLAR burnout");
		akku_discharge(AKKU, 0); // enable discharge no limit
		//	solar_override_seconds("küche", WAIT_BURNOUT);
		//	solar_override_seconds("wozi", WAIT_BURNOUT);
	}

	// set akku to DISCHARGE if we have long term grid download
	// TODO verify
	if ((GSTATE_GRID_DLOAD && GSTATE_OFFLINE && !AKKU_DISCHARGING) || (GSTATE_GRID_DLOAD && !GSTATE_OFFLINE && !AKKU_CHARGING)) {
		int tiny_tomorrow = gstate->tomorrow < params->akku_capacity;

		// winter: limit discharge and try to extend ttl as much as possible
		int limit = GSTATE_WINTER && (tiny_tomorrow || gstate->survive < 0) ? AKKU_LIMIT_DISCHARGE : 0;
		akku_discharge(AKKU, limit);

		// minimum SOC: standard 5%, winter and tomorrow not much PV expected 10%
		int min_soc = GSTATE_WINTER && tiny_tomorrow && gstate->soc > 111 ? 10 : 5;
		akku_set_min_soc(min_soc);
	}

	// reset FLAG_STANDBY_CHECKED on permanent OVERLOAD_STANDBY_FORCE
	if (dstate->rload > OVERLOAD_STANDBY_FORCE && DSTATE_LAST5->rload > OVERLOAD_STANDBY_FORCE && DSTATE_LAST10->rload > OVERLOAD_STANDBY_FORCE)
		for (device_t **dd = DEVICES; *dd; dd++)
			DD->flags &= ~FLAG_STANDBY_CHECKED;
}

// set device into MANUAL mode and toggle power
void solar_toggle_name(const char *name) {
	device_t *d = get_by_name(name);
	if (!d)
		return;

	d->state = Manual;
	toggle_device(d);
}

// set device into MANUAL mode and toggle power
void solar_toggle_id(unsigned int id, int relay) {
	device_t *d = get_by_id(id, relay);
	if (!d)
		return;

	d->state = Manual;
	toggle_device(d);
}

// update device status from tasmota mqtt response
void solar_tasmota(tasmota_t *t) {
	device_t *d = get_by_id(t->id, t->relay);
	if (!d)
		d = get_by_id(t->id, 0);
	if (!d)
		return;

	if (d->state == Initial)
		d->state = Auto;
	if (d->adj) {
		d->power = t->gp8403_pc0;
		d->load = t->gp8403_pc0 * d->total / 100;
	} else {
		d->power = t->power;
		d->load = t->power ? d->total : 0;
	}
	xlog("SOLAR update id=%06X relay=%d power=%d load=%d name=%s", d->id, d->r, d->power, d->load, d->name);
}

static void loop() {
	time_t now_ts;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// wait for tasmota discovery + sensor update
	sleep(1);

	// ask for initial power states
#ifndef SOLAR_MAIN
	for (device_t **dd = DEVICES; *dd; dd++) {
		if (DD->adj)
			tasmota_status_ask(DD->id, 10);
		else
			tasmota_power_ask(DD->id, DD->r);
	}
#endif

	// wait for power state responses
	sleep(1);

	// get initial akku state
	akku_state(AKKU);

	// initially select the program of the day
	choose_program();

	// dispatcher main loop
	while (1) {

		// PROFILING_START

		// get actual time and store global
		now_ts = time(NULL);
		localtime_r(&now_ts, &now_tm);

		// count down lock
		if (dstate->lock)
			dstate->lock--;

		// check response
		response();

		if (PSTATE_EMERGENCY)
			emergency();

		// calculate device state and actions
		calculate_dstate(now_ts);

		if (DSTATE_ACTION_RAMP)
			ramp();

		if (DSTATE_ACTION_STANDBY)
			standby();

		if (DSTATE_ACTION_STEAL)
			steal();

		// cron jobs
		if (MINLY)
			minly();
		if (HOURLY)
			hourly();
		if (DAILY)
			daily();

		// print dstate once per minute / on device action
		if (MINLY || DSTATE_ACTION)
			print_dstate();

		// web output
		create_dstate_json();
		create_devices_json();

		// PROFILING_LOG("dispatcher main loop")

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
		DD->state = Initial;

		// get IP address of boilers and disable when failed
		if (DD->adj) {
			DD->addr = resolve_ip(DD->name);
			if (DD->addr == 0)
				DD->state = Disabled;
		}
	}

	return 0;
}

static void stop() {
	if (sock)
		close(sock);
}

MCP_REGISTER(solar_dispatcher, 11, &init, &stop, &loop);
