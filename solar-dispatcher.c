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

// device ramp signatures
static int ramp_heater(device_t *device, int power);
static int ramp_boiler(device_t *device, int power);
static int ramp_akku(device_t *device, int power);

// devices
static device_t a1 = { .name = "akku", .total = 0, .ramp = &ramp_akku, .adj = 0 }, *AKKU = &a1;
static device_t b1 = { .name = "boiler1", .id = BOILER1, .total = 2000, .ramp = &ramp_boiler, .adj = 1, .r = 0 };
static device_t b2 = { .name = "boiler2", .id = BOILER2, .total = 2000, .ramp = &ramp_boiler, .adj = 1, .r = 0 };
static device_t b3 = { .name = "boiler3", .id = BOILER3, .total = 2000, .ramp = &ramp_boiler, .adj = 1, .r = 0, .from = 11, .to = 15, .min = 100 };
static device_t h1 = { .name = "küche", .id = SWITCHBOX, .total = 500, .ramp = &ramp_heater, .adj = 0, .r = 1, .host = "switchbox" };
static device_t h2 = { .name = "wozi", .id = SWITCHBOX, .total = 500, .ramp = &ramp_heater, .adj = 0, .r = 2, .host = "switchbox" };
static device_t h3 = { .name = "schlaf", .id = PLUG5, .total = 500, .ramp = &ramp_heater, .adj = 0, .r = 0, .host = "plug5" };
static device_t h4 = { .name = "tisch", .id = SWITCHBOX, .total = 200, .ramp = &ramp_heater, .adj = 0, .r = 3, .host = "switchbox" };

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

static int ramp_heater(device_t *heater, int power) {
	heater->delta = 0;
	if (!power || heater->state == Disabled || heater->state == Initial || heater->state == Standby)
		return 0; // 0 - continue loop

	// heating disabled
	if (power > 0 && !GSTATE_HEATING && heater->state != Manual) {
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

	// check if update is necessary
	if (heater->power == power)
		return 0;

	if (power)
		xdebug("SOLAR switching %s ON", heater->name);
	else
		xdebug("SOLAR switching %s OFF", heater->name);

#ifndef SOLAR_MAIN
	tasmota_power(heater->id, heater->r, power ? 1 : 0);
#endif

	// update power values
	dstate->lock = WAIT_RESPONSE;
	heater->delta = power ? heater->total : heater->total * -1;
	heater->load = power ? heater->total : 0;
	heater->power = power;
	return 1; // loop done
}

// echo p:0:0 | socat - udp:boiler3:1975
// for i in `seq 1 10`; do let j=$i*10; echo p:$j:0 | socat - udp:boiler1:1975; sleep 1; done
static int ramp_boiler(device_t *boiler, int power) {
	boiler->delta = 0;
	if (!power || boiler->state == Disabled || boiler->state == Initial || boiler->state == Standby)
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

	// charging boilers only between configured FROM / TO (winter always)
	if (boiler->power == 0 && power > 0 && !GSTATE_WINTER && boiler->from && boiler->to && (now->tm_hour < boiler->from || now->tm_hour >= boiler->to)) {
		boiler->state = Standby;
		return 0;
	}

	// not enough to start up - electronic thermostat struggles at too less power
	if (boiler->state == Auto && boiler->power == 0 && power < boiler->min)
		return 0;

	// power steps
	int step = power * 100 / boiler->total;
	if (power < 0 && (power < step * boiler->total / 100))
		step -= 1; // one step more when not enough
	if (!step)
		return 0;

	// do single steps at warm up due to much smaller cold resistance
	if (boiler->power < 5 && 1 < step && step < 5)
		step = 1;

	// transform power into 0..100%
	power = boiler->power + step;

	// electronic thermostat - leave boiler alive when in AUTO mode
	int min = boiler->state == Auto && boiler->min && !FORCE_OFF(boiler) && !PSTATE_OFFLINE ? boiler->min * 100 / boiler->total : 0;

	HICUT(power, 100);
	LOCUT(power, min);

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
	dstate->lock = boiler->power == 0 ? WAIT_THERMOSTAT : WAIT_RESPONSE; // electronic thermostat takes more time at startup
	boiler->delta = (power - boiler->power) * boiler->total / 100;
	boiler->load = power * boiler->total / 100;
	boiler->power = power;
	return 1; // loop done
}

static int ramp_akku(device_t *akku, int power) {
	akku->delta = 0;

	// ramp down request
	if (power < 0) {

		// leave akku charging to avoid grid load - forward to next device if akku is discharging or not in CHARGE mode
		if (akku->load < MINIMUM || !AKKU_CHARGING)
			return 0; // continue loop

		// akku ramps down itself when charging
		// if ramp down is smaller than actual charge power we can fully consume it
		int consumed = power * -1 < akku->load;

		// release up to current charging power
		akku->delta = consumed ? power : akku->load * -1;

		// forward to next device if we were not able to consume it fully
		return consumed;
	}

	// ramp up request
	if (power > 0) {

		// set into standby when full and forward to next device
		if (gstate->soc == 1000) {
			akku_standby(akku);
			return 0; // continue loop
		}

		// forward to next device when akku in CHARGE mode but not charging / on extra power
		if (AKKU_CHARGING && PSTATE_GRID_ULOAD)
			return 0; // continue loop

		// start charging
		// TODO limit basierend auf pvmin/pvmax/pvavg setzen
		int limit = 0;
		if (GSTATE_SUMMER || gstate->today > params->akku_capacity * 2)
			limit = AKKU_LIMIT_CHARGE2X;
		if (GSTATE_SUMMER || gstate->today > params->akku_capacity * 3)
			limit = AKKU_LIMIT_CHARGE3X;
		if (!akku_charge(akku, limit)) {
			dstate->lock = WAIT_START_CHARGE;
			return 0;
		}

		// expect to consume all available DC power up to either battery's charge maximum or limit when set
		akku->delta = pstate->mppt1 + pstate->mppt2 - akku->load;
		if (akku->delta > akku->total)
			akku->delta = akku->total;

		// forward to next device if charging is nearly saturated
		return akku->power > 95 ? 0 : 1;
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
#define DEVICE_TEMPLATE	"{\"id\":\"%06X\", \"r\":\"%d\", \"name\":\"%s\", \"host\":\"%s\", \"state\":%d, \"power\":%d, \"flags\":%d, \"total\":%d, \"load\":%d, \"steal\":%d}"
		fprintf(fp, DEVICE_TEMPLATE, DD->id, DD->r, DD->name, DD->host, DD->state, DD->power, DD->flags, DD->total, DD->load, DD->steal);
	}
	fprintf(fp, "]");

	fflush(fp);
	fclose(fp);
}

static void print_dstate() {
	char line[512], value[16]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "DSTATE ");
	xlogl_bits16(line, NULL, dstate->flags);
	if (!PSTATE_OFFLINE) {
		xlogl_int(line, "CLoad", dstate->cload);
		xlogl_int(line, "RLoad", dstate->rload);
		strcat(line, "   ");
	}
	for (device_t **dd = potd->devices; *dd; dd++) {
		switch (DD->state) {
		case Disabled:
			snprintf(value, 5, " .");
			break;
		case Initial:
			snprintf(value, 5, " i");
			break;
		case Standby:
			snprintf(value, 5, " S");
			break;
		case Manual:
			snprintf(value, 5, " %c", DD->power ? 'M' : 'm');
			break;
		case Auto:
			if (DD->adj) {
				snprintf(value, 5, " %3d", DD->power);
				if (ACTIVE_CHECKED(DD))
					strcat(value, "!");
				break;
			}
			if (ACTIVE_CHECKED(DD))
				snprintf(value, 5, " %c", DD->power ? 'X' : '_');
			else
				snprintf(value, 5, " %c", DD->power ? 'x' : '_');
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

	strcat(line, "   potd ");
	strcat(line, potd ? potd->name : "NULL");

	if (dstate->lock)
		xlogl_int(line, "   Lock", dstate->lock);

	xlogl_end(line, strlen(line), 0);
}

static int toggle_device(device_t *d) {
	xdebug("SOLAR toggle id=%06X relay=%d power=%d load=%d name=%s", d->id, d->r, d->power, d->load, d->name);
	if (!d->power)
		return (d->ramp)(d, d->total);
	else
		return (d->ramp)(d, d->total * -1);
}

static int ramp_device(device_t *d, int power) {
	if (d->state == Manual)
		return 0;

	if (power < 0)
		xdebug("SOLAR ramp↓ %d %s", power, d->name);
	else if (power > 0)
		xdebug("SOLAR ramp↑ +%d %s", power, d->name);
	else
		xdebug("SOLAR ramp 0 %s", d->name);

	// call device specific ramp function
	return (d->ramp)(d, power);
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
	if (GSTATE_CHARGE_AKKU && gstate->soc < 100)
		return select_program(&MODEST);

	// we will NOT survive - charging akku has priority
	if (GSTATE_CHARGE_AKKU && gstate->survive < 1000)
		return select_program(&MODEST);

	// survive but tomorrow not enough PV - charging akku has priority
	if (GSTATE_CHARGE_AKKU && GSTATE_WINTER && gstate->tomorrow < params->akku_capacity)
		return select_program(&MODEST);

	// forecast below 50% and akku not yet enough to survive
	if (GSTATE_CHARGE_AKKU && gstate->forecast < 500 && AKKU_AVAILABLE < gstate->nsurvive)
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

static void burnout() {
	xlog("SOLAR burnout");
	akku_discharge(AKKU, 0); // enable discharge no limit
//	solar_override_seconds("küche", WAIT_BURNOUT);
//	solar_override_seconds("wozi", WAIT_BURNOUT);
}

static void rampup() {
	if (!PSTATE_VALID || PSTATE_EMERGENCY || DSTATE_ALL_UP || DSTATE_ALL_STANDBY)
		return;

	device_t **dd = potd->devices;
	while (*dd && dstate->ramp > 0) {
		if (ramp_device(DD, dstate->ramp)) {
			int power = dstate->ramp;
			dstate->ramp -= DD->delta;
			xlog("SOLAR ramped↑ %s power=%d delta=%d ramp=%d", DD->name, power, DD->delta, dstate->ramp);
			msleep(111);
		}
		dd++;
	}
}

static void rampdown() {
	if (!PSTATE_VALID || DSTATE_ALL_DOWN || DSTATE_ALL_STANDBY)
		return;

	// jump to last entry
	device_t **dd = potd->devices;
	while (*dd)
		dd++;

	// now go backward - this gives reverse order
	while (dd-- != potd->devices && dstate->ramp < 0)
		if (ramp_device(DD, dstate->ramp)) {
			int power = dstate->ramp;
			dstate->ramp -= DD->delta;
			xlog("SOLAR ramped↓ %s power=%d delta=%d ramp=%d", DD->name, power, DD->delta, dstate->ramp);
			msleep(111);
		}
}

static void ramp() {
	if (dstate->lock)
		return; // no action when locked

	dstate->ramp = pstate->surp;
	if (!dstate->ramp)
		return; // nothing to ramp

	if (dstate->ramp <= -RAMP)
		rampdown();

	// allow rampup after rampdown if power was released

	if (dstate->ramp >= -RAMP)
		rampup();
}

static int perform_standby(device_t *d) {
	int power = d->adj ? (d->power < 50 ? +500 : -500) : (d->power ? d->total * -1 : d->total);
	xlog("SOLAR starting standby check on %s with power=%d", d->name, power);

	// store meter power
	int p1 = pstate->p1;
	int p2 = pstate->p2;
	int p3 = pstate->p3;

	// ramp
	ramp_device(d, power);

	// wait for response
	sleep(WAIT_RESPONSE);

	// calculate delta power per phase
	int d1 = pstate->p1 - p1;
	if (-NOISE < d1 && d1 < NOISE)
		d1 = 0;
	int d2 = pstate->p2 - p2;
	if (-NOISE < d2 && d2 < NOISE)
		d2 = 0;
	int d3 = pstate->p3 - p3;
	if (-NOISE < d3 && d3 < NOISE)
		d3 = 0;

	// valid response is at least 2/3 of last ramp
	int delta = d->delta - d->delta / 3;
	d->delta = 0; // reset

	// check if we got a response on any phase
	int l1 = delta > 0 ? d1 > delta : d1 < delta;
	int l2 = delta > 0 ? d2 > delta : d2 < delta;
	int l3 = delta > 0 ? d3 > delta : d3 < delta;

	if (l1 || l2 || l3) {
		// standby check was negative - we got a response
		xlog("SOLAR standby check negative for %s, delta expected %d actual %d %d %d", d->name, delta, d1, d2, d3);
		d->flags |= FLAG_ACTIVE_CHECKED; // flag with standby check performed
	} else {
		// standby check was positive -> set device into standby
		xlog("SOLAR standby check positive for %s, delta expected %d actual %d %d %d  --> entering standby", d->name, delta, d1, d2, d3);
		d->flags |= FLAG_FORCE_OFF;
		ramp_device(d, d->total * -1);
		d->state = Standby;
	}

	return 0;
}

static int standby() {
	if (!PSTATE_VALID || PSTATE_EMERGENCY || PSTATE_OFFLINE || !DSTATE_CHECK_STANDBY || DSTATE_ALL_STANDBY || dstate->lock || pstate->pload < 120)
		return 0; // only when !locked and surplus is not negative

	// try first active powered adjustable device
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD->state == Auto && !ACTIVE_CHECKED(DD) && DD->power && DD->adj)
			return perform_standby(DD);

	// try first active powered dumb device
	for (device_t **dd = DEVICES; *dd; dd++)
		if (DD->state == Auto && !ACTIVE_CHECKED(DD) && DD->power)
			return perform_standby(DD);

	return 0;
}

static void steal() {
	dstate->steal = 0;
	if (!PSTATE_VALID || PSTATE_EMERGENCY || PSTATE_OFFLINE || DSTATE_CHECK_STANDBY || DSTATE_ALL_STANDBY || DSTATE_ALL_UP || dstate->lock || pstate->pload < 120)
		return; // only when !locked and surplus is not negative

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
	if (dstate->steal < NOISE)
		return;

	// check if we can steal from lower prioritized devices
	for (device_t **tt = potd->devices; *tt; tt++) {
		device_t *t = *tt; // thief

		// akku can only steal inverters ac output when charging and not saturated (limited or maximum charge power reached)
		if (t == AKKU && AKKU_CHARGING && pstate->ac1 > NOISE && AKKU->power < 95) {
			dstate->steal = pstate->ac1;

			// jump to last entry
			device_t **vv = potd->devices;
			while (*vv)
				vv++;

			// ramp down victims in inverse order
			int to_steal = dstate->steal;
			while (--vv != tt && to_steal > 0) {
				device_t *v = *vv;
				ramp_device(v, v->steal * -1);
				int given = v->delta * -1;
				xlog("SOLAR steal thief=AKKU to_steal=%d victim=%s given=%d", to_steal, v->name, given);
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
		if (dstate->steal < NOISE)
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
			ramp_device(v, v->steal * -1);
			int given = v->delta * -1;
			xlog("SOLAR steal thief=%s ramp=%d min=%d to_steal=%d victim=%s given=%d", t->name, total, min, to_steal, v->name, given);
			to_steal -= given;
		}

		// ramp up thief
		ramp_device(t, total);
		dstate->lock = AKKU_CHARGING && !PSTATE_EXTRAPOWER ? WAIT_AKKU : WAIT_RESPONSE;
		return;
	}
}

static void calculate_dstate() {
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

	// indicate standby check on permanent OVERLOAD_STANDBY
	if (dstate->rload > OVERLOAD_STANDBY && DSTATE_LAST5->rload > OVERLOAD_STANDBY && DSTATE_LAST10->rload > OVERLOAD_STANDBY) {
		dstate->flags |= FLAG_CHECK_STANDBY;
		xdebug("SOLAR set FLAG_CHECK_STANDBY load=%d cload=%d rload=%d", pstate->load, dstate->cload, dstate->rload);
	}

	// copy to history
	memcpy(DSTATE_NOW, (void*) dstate, sizeof(dstate_t));
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
	}
}

static void minly() {
	// force off when offline - except manual
	if (PSTATE_OFFLINE)
		for (device_t **dd = DEVICES; *dd; dd++)
			if (DD->state != Manual)
				ramp_device(DD, DD->total * -1);

	// reset ACTIVE_CHECKED on permanent OVERLOAD_STANDBY_FORCE
	if (dstate->rload > OVERLOAD_STANDBY_FORCE && DSTATE_LAST5->rload > OVERLOAD_STANDBY_FORCE && DSTATE_LAST10->rload > OVERLOAD_STANDBY_FORCE)
		for (device_t **dd = DEVICES; *dd; dd++)
			DD->flags &= ~FLAG_ACTIVE_CHECKED;

	// update akku state
	akku_state(AKKU);

	// set akku to DISCHARGE if we have long term grid download
	// TODO verify
	if ((PSTATE_GRID_DLOAD && PSTATE_OFFLINE && !AKKU_DISCHARGING) || (PSTATE_GRID_DLOAD && !PSTATE_OFFLINE && !AKKU_CHARGING)) {
		int tiny_tomorrow = gstate->tomorrow < params->akku_capacity;

		// winter: limit discharge and try to extend ttl as much as possible
		int limit = GSTATE_WINTER && (tiny_tomorrow || gstate->survive < 0) ? AKKU_LIMIT_DISCHARGE : 0;
		akku_discharge(AKKU, limit);

		// minimum SOC: standard 5%, winter and tomorrow not much PV expected 10%
		int min_soc = GSTATE_WINTER && tiny_tomorrow && gstate->soc > 111 ? 10 : 5;
		akku_set_min_soc(min_soc);
	}

	// choose potd
	choose_program();
}

// set device into MANUAL mode and toggle power
int solar_toggle_name(const char *name) {
	device_t *d = get_by_name(name);
	if (!d)
		return 0;

	d->state = Manual;
	return toggle_device(d);
}

// set device into MANUAL mode and toggle power
int solar_toggle_id(unsigned int id, int relay) {
	device_t *d = get_by_id(id, relay);
	if (!d)
		return 0;

	d->state = Manual;
	return toggle_device(d);
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

		// emergency mode
		if (PSTATE_EMERGENCY)
			emergency();

		// burnout mode
		if (PSTATE_BURNOUT)
			burnout();

		// count down lock
		if (dstate->lock)
			dstate->lock--;

		// ramp
		ramp();

		// standby logic each 10 seconds (1, 11, 21, ...)
		if (now_ts % 10 == 1)
			standby();

		// steal logic each 10 seconds (3, 13, 23, ...)
		if (now_ts % 10 == 3)
			steal();

		// cron jobs
		if (MINLY) {
			minly();

			if (HOURLY) {
				hourly();

				if (DAILY)
					daily();
			}
		}

		// calculate device state
		calculate_dstate();

		// print dstate once per minute / on device action
		if (MINLY || dstate->lock == WAIT_RESPONSE)
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
