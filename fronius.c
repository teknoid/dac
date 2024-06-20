#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <curl/curl.h>

#include "fronius-config.h"
#include "fronius.h"
#include "tasmota.h"
#include "frozen.h"
#include "utils.h"
#include "mcp.h"

// program of the day - mosmix will chose appropriate program
static potd_t *potd;

// global state with power flow data and calculations
static state_t *state;
static state_t history[HISTORY];
static int history_ptr = 0;

static struct tm *now;
static int sock = 0;

int set_heater(device_t *heater, int power) {
	// fix power value if out of range
	if (power < 0)
		power = 0;
	if (power > 1)
		power = 1;

	// check if update is necessary
	if (heater->power && heater->power == power)
		return 0;

	// can we send a message
	if (heater->addr == NULL)
		return xerr("No address to send HTTP message");

	// char command[128];
	if (power) {
		// xlog("FRONIUS switching %s ON", heater->name);
		// snprintf(command, 128, "curl --silent --output /dev/null http://%s/cm?cmnd=Power%%20On", heater->addr);
		// system(command);
		tasmota_power(heater->id, 0, 1);
	} else {
		// xlog("FRONIUS switching %s OFF", heater->name);
		// snprintf(command, 128, "curl --silent --output /dev/null http://%s/cm?cmnd=Power%%20Off", heater->addr);
		// system(command);
		tasmota_power(heater->id, 0, 0);
	}

	// update power values
	heater->power = power;
	heater->dload = power ? heater->load * -1 : heater->load;
	return 1; // loop done
}

// echo p:0:0 | socat - udp:boiler3:1975
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
		return xerr("No address to send UDP message");

	// calculate step
	int step = power - boiler->power;

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock == 0)
		return xerr("Error creating socket");

	// write IP and port into sockaddr structure
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

	// update power values
	boiler->power = power;
	boiler->dload = boiler->load * step / -100;
	return 1; // loop done
}

static void set_all_devices(int power) {
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = devices[i];
		(d->set_function)(d, power);
	}
}

// initialize all devices with start values
static void init_all_devices() {
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = devices[i];
		d->state = Active;
		d->power = -1;
		d->dload = 0;
		d->addr = resolve_ip(d->name);
	}
}

static state_t* get_history(int offset) {
	int i = history_ptr + offset;
	if (i < 0)
		i += HISTORY;
	if (i >= HISTORY)
		i -= HISTORY;
	return &history[i];
}

static void dump_history(int back) {
	char line[sizeof(state_t) * 8 + 16];
	char value[8];

	strcpy(line, "FRONIUS History  idx    pv   Δpv   grid  akku  surp  grdy modst steal waste   sum  chrg  load Δload  pv10   pv7  dist  tend  wait");
	xdebug(line);
	for (int y = 0; y < back; y++) {
		strcpy(line, "FRONIUS History ");
		snprintf(value, 16, "[%2d] ", y * -1);
		strcat(line, value);
		int *vv = (int*) get_history(y * -1);
		for (int x = 0; x < sizeof(state_t) / sizeof(int); x++) {
			snprintf(value, 16, x == 2 ? "%6d " : "%5d ", vv[x]);
			strcat(line, value);
		}
		xdebug(line);
	}
}

static void print_power_status(const char *message) {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS");
	xlogl_int_b(line, "PV", state->pv);
	xlogl_int(line, 1, 1, "Grid", state->grid);
	xlogl_int(line, 1, 1, "Akku", state->akku);
	xlogl_int(line, 1, 0, "Surp", state->surplus);
	xlogl_int(line, 1, 0, "Greedy", state->greedy);
	xlogl_int(line, 1, 0, "Modest", state->modest);
	xlogl_int_B(line, "Load", state->load);
	xlogl_int_B(line, "ΔLoad", state->dload);
	xlogl_int(line, 0, 0, "PV10", state->pv10);
	xlogl_int(line, 0, 0, "PV7", state->pv7);
	xlogl_int(line, 0, 0, "Chrg", state->chrg);
	xlogl_int(line, 0, 0, "Sum", state->sum);
	xlogl_int(line, 0, 0, "Dist", state->distortion);
	xlogl_end(line, sizeof(line), message);
}

static void print_device_status(int wait, int next_reset) {
	char message[128];
	char value[5];

	strcpy(message, "FRONIUS device state ");
	for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++) {
		snprintf(value, 5, "%d", (*ds)->device->state);
		strcat(message, value);
	}

	strcat(message, "   power ");
	for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++) {
		snprintf(value, 5, " %3d", (*ds)->device->power);
		strcat(message, value);
	}

	strcat(message, "   next reset in ");
	append_timeframe(message, next_reset);

	strcat(message, "   wait ");
	snprintf(value, 5, "%d", wait);
	strcat(message, value);

	xlog(message);
}

static CURL* curl_init(const char *url, _curl_write_callback2 cb) {
	CURL *curl = curl_easy_init();
	if (curl != NULL) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
		curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 4096);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 30L);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
	}
	return curl;
}

static int curl_perform(CURL *curl) {
	CURLcode ret = curl_easy_perform(curl);
	if (ret != CURLE_OK)
		return xerrr(-1, "FRONIUS curl perform error %d", ret);

	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	if (http_code != 200)
		return xerrr(-2, "FRONIUS response code %d", http_code);

	return 0;
}

static size_t callback_fronius10(const char *data, size_t size, size_t nmemb, const void *userdata) {
	size_t realsize = size * nmemb;
	xdebug("FRONIUS callback_fronius10() size %d nmemb %d realsize %d", size, nmemb, realsize);

	if (nmemb < 32)
		return realsize; // noise

	float p_charge, p_akku, p_grid, p_load, p_pv;
	char *c;
	int ret;

	ret = json_scanf(data, realsize, "{ Body { Data { Site { P_Akku:%f, P_Grid:%f, P_Load:%f, P_PV:%f } } } }", &p_akku, &p_grid, &p_load, &p_pv);
	if (ret != 4)
		xlog("FRONIUS callback_fronius10() warning! parsing Body->Data->Site: expected 4 values but got only %d", ret);

	state->akku = p_akku;
	state->grid = p_grid;
	state->load = p_load;
	state->pv10 = p_pv;

	// workaround parsing { "Inverters" : { "1" : { ... } } }
	ret = json_scanf(data, realsize, "{ Body { Data { Inverters:%Q } } }", &c);
	if (ret == 1 && c != NULL) {
		char *p = c;
		while (*p != '{')
			p++;
		p++;
		while (*p != '{')
			p++;

		ret = json_scanf(p, strlen(p) - 1, "{ SOC:%f }", &p_charge);
		if (ret == 1)
			state->chrg = p_charge;
		else {
			xlog("FRONIUS callback_fronius10() warning! parsing Body->Data->Inverters->SOC: no result");
			state->chrg = 0;
		}

		free(c);
	} else
		xlog("FRONIUS callback_fronius10() warning! parsing Body->Data->Inverters: no result");

	return realsize;
}

static size_t callback_fronius7(const char *data, size_t size, size_t nmemb, const void *userdata) {
	size_t realsize = size * nmemb;
	xdebug("FRONIUS callback_fronius7() size %d nmemb %d realsize %d", size, nmemb, realsize);

	if (nmemb < 32)
		return realsize; // noise

	float p_pv;
	int ret = json_scanf(data, realsize, "{ Body { Data { Site { P_PV:%f } } } }", &p_pv);
	if (ret == 1)
		state->pv7 = p_pv;
	else {
		xlog("FRONIUS callback_fronius7() warning! parsing Body->Data->Site->P_PV: no result");
		state->pv7 = 0;
	}

	return realsize;
}

static size_t callback_calibrate(const char *data, size_t size, size_t nmemb, const void *userdata) {
	size_t realsize = size * nmemb;
	xdebug("FRONIUS callback_calibrate() size %d nmemb %d realsize %d", size, nmemb, realsize);

	if (nmemb < 32)
		return realsize; // noise

	float p_grid;
	int ret = json_scanf(data, realsize, "{ Body { Data { PowerReal_P_Sum:%f } } }", &p_grid);
	if (ret == 1)
		state->grid = p_grid;
	else {
		xlog("FRONIUS callback_calibrate() warning! parsing Body->Data->PowerReal_P_Sum: no result");
		state->grid = 0;
	}

	return realsize;
}

static int choose_program(const potd_t *p) {
	xlog("FRONIUS choosing %s program of the day", p->name);
	potd = (potd_t*) p;
	return 0;
}

static int mosmix() {
	char line[8];
	int m0, m1, m2;

	// default program
	potd = (potd_t*) &CLOUDY_EMPTY;

	FILE *fp = fopen(MOSMIX, "r");
	if (fp == NULL)
		return xerr("FRONIUS no mosmix data available");

	if (fgets(line, 8, fp) != NULL)
		if (sscanf(line, "%d", &m0) != 1)
			return xerr("FRONIUS mosmix parse error %s", line);

	if (fgets(line, 8, fp) != NULL)
		if (sscanf(line, "%d", &m1) != 1)
			return xerr("FRONIUS mosmix parse error %s", line);

	if (fgets(line, 8, fp) != NULL)
		if (sscanf(line, "%d", &m2) != 1)
			return xerr("FRONIUS mosmix parse error %s", line);

	fclose(fp);

	int e0 = m0 * MOSMIX_FACTOR;
	int e1 = m1 * MOSMIX_FACTOR;
	int e2 = m2 * MOSMIX_FACTOR;
	int na = (100 - state->chrg) * (AKKU_CAPACITY / 100);
	int ns = (24 - now->tm_hour) * (SELF_CONSUMING / 24);
	int n = na + ns;

	xlog("FRONIUS mosmix needed %d (%d akku + %d self), Rad1h/expected today %d/%d tomorrow %d/%d tomorrow+1 %d/%d", n, na, ns, m0, e0, m1, e1, m2, e2);

	if (e0 > n)
		return choose_program(&SUNNY);

	if (state->chrg > 50 && e1 > SELF_CONSUMING)
		return choose_program(&TOMORROW);

	if (state->chrg > 75)
		return choose_program(&CLOUDY_FULL);

	return choose_program(&CLOUDY_EMPTY);
}

static int calculate_step(device_t *d, int power) {
	// power steps
	int step = power / (d->load / 100);
	xdebug("FRONIUS step1 %d", step);

	// when we have distortion, do: smaller up steps / bigger down steps
	if (state->distortion) {
		if (step > 0)
			step /= (state->distortion == 1 ? 2 : state->distortion);
		else
			step *= (state->distortion == 1 ? 2 : state->distortion);
		xdebug("FRONIUS step2 %d", step);
	}

	// we need at least one step if power is not null
	if (!step) {
		if (power < 0)
			step = state->tendence < 0 ? -2 : -1;
		else if (power > 0)
			step = state->tendence > 0 ? 2 : 1;
		xdebug("FRONIUS step3 %d", step);
	}

	return step;
}

static int ramp_adjustable(device_t *d, int power) {
	xdebug("FRONIUS ramp_adjustable() %s %d", d->name, power);

	// standby check requested - do a big ramp
	if (d->state == Request_Standby_Check) {
		xdebug("FRONIUS starting standby check on %s", d->name);
		d->state = Standby_Check;
		if (d->power < 50)
			return (d->set_function)(d, d->power + 25);
		else
			return (d->set_function)(d, d->power - 25);
	}

	// already full up
	if (d->power == 100 && power > 0)
		return 0;

	// already full down
	if (!d->power && power < 0)
		return 0;

	int step = calculate_step(d, power);
	return (d->set_function)(d, d->power + step);
}

static int ramp_dumb(device_t *d, int power) {
	int min = d->load + d->load * state->distortion / 10;
	xdebug("FRONIUS ramp_dumb() %s %d (min %d)", d->name, power, min);

	// standby check requested - toggle
	if (d->state == Request_Standby_Check) {
		xdebug("FRONIUS starting standby check on %s", d->name);
		d->state = Standby_Check;
		if (d->power)
			return (d->set_function)(d, 0);
		else
			return (d->set_function)(d, 1);
	}

	// keep on as long as we have enough power and device is already on
	if (d->power && power > 0)
		return 0; // continue loop

	// switch on when enough power is available
	if (!d->power && power > min)
		return (d->set_function)(d, 1);

	// switch off
	if (d->power)
		return (d->set_function)(d, 0);

	return 0; // continue loop
}

static int rampup_device(device_t *d, int power) {
	if (d->state == Standby)
		return 0; // continue loop

	if (d->adjustable)
		return ramp_adjustable(d, power);
	else
		return ramp_dumb(d, power);
}

static int rampdown_device(device_t *d, int power) {
	if (d->adjustable)
		return ramp_adjustable(d, power);
	else
		return ramp_dumb(d, power);
}

static device_t* rampup(int power, int only_greedy) {
	xdebug("FRONIUS rampup() %d %s", power, only_greedy ? "greedy" : "modest");
	for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++) {
		if (only_greedy && !(*ds)->greedy)
			continue; // skip non-greedy devices
		if (rampup_device((*ds)->device, power))
			return (*ds)->device;
	}

	return 0; // next priority
}

static device_t* rampdown(int power, int skip_greedy) {
	xdebug("FRONIUS rampdown() %d %s", power, skip_greedy ? "modest" : "greedy");
	const potd_device_t **ds = potd->devices;

	// jump to last entry
	while (*ds != NULL)
		ds++;
	ds--;

	// now go backward - this will give a reverse order
	do {
		if (skip_greedy && (*ds)->greedy)
			continue; // skip greedy devices
		if (rampdown_device((*ds)->device, power))
			return (*ds)->device;
	} while (ds-- != potd->devices);

	return 0; // next priority
}

// do device adjustments in sequence of priority
static device_t* ramp() {
	device_t *d;

	// 1. no extra power available: ramp down devices but skip greedy
	if (state->modest < 0) {
		d = rampdown(state->modest, 1);
		if (d)
			return d;
	}

	// 2. consuming grid power or discharging akku: ramp down all devices
	if (state->greedy < 0) {
		d = rampdown(state->greedy, 0);
		if (d)
			return d;
	}

	// 3. uploading grid power or charging akku: ramp up only greedy devices
	if (state->greedy > 0) {
		d = rampup(state->greedy, 1);
		if (d)
			return d;
	}

	// 4. extra power available: ramp up all devices
	if (state->modest > 0) {
		d = rampup(state->modest, 0);
		if (d)
			return d;
	}

	return 0;
}

static void steal_power() {
	int dpower = 0, apower = 0, greedy_dumb_off = 0;

	// check if we have greedy dumb off devices
	// collect non greedy adjustable power and greedy dumb power
	for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++) {
		device_t *d = (*ds)->device;

		if ((*ds)->greedy && !d->adjustable && !d->power)
			greedy_dumb_off = 1;

		if (!d->power)
			continue;

		if (!(*ds)->greedy && d->adjustable)
			apower += d->load * d->power / 100;

		if ((*ds)->greedy && !d->adjustable)
			dpower += d->load;
	}

	// nothing to steal
	if (!greedy_dumb_off || !apower)
		return;

	// a greedy dumb off device can steal power from a non greedy adjustable device if this power is really consumed
	int spower = state->load * -1 - dpower - BASELOAD;
	if (spower < 0)
		spower = 0;
	state->steal = spower < apower ? spower : apower;
	state->greedy += state->steal;
	xdebug("FRONIUS steal_power() %d load:%d dpower:%d apower:%d spower:%d off:%d", state->steal, state->load, dpower, apower, spower, greedy_dumb_off);
}

static void check_standby() {
	if (state->distortion)
		return; // standby check is not reliable

	// force standby check on all devices if we have no load at all
	if (BASELOAD * -1 < state->load) {
		for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++)
			if ((*ds)->device->power)
				(*ds)->device->state = Request_Standby_Check;
		return;
	}

	if (state->dload > BASELOAD) {
		xdebug("FRONIUS power released by someone %d, requesting standby check on thermostat devices", state->dload);
		for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++)
			if ((*ds)->device->thermostat && (*ds)->device->power)
				(*ds)->device->state = Request_Standby_Check;
	}
}

static void check_response(device_t *d) {

	// do we have a valid response - at least 50% of expected?
	int response = state->dload != 0 && (d->dload > 0 ? (state->dload > d->dload / 2) : (state->dload < d->dload / 2));

	// response OK
	if (d->state == Active && response) {
		xdebug("FRONIUS response OK from %s, delta load expected %d actual %d", d->name, d->dload, state->dload);
		d->dload = 0;
		return;
	}

	// standby check was negative - we got a response
	if (d->state == Standby_Check && response) {
		xdebug("FRONIUS standby check negative for %s, delta load expected %d actual %d", d->name, d->dload, state->dload);
		(d->set_function)(d, 0);
		d->state = Active;
		d->dload = 0;
		return;
	}

	// standby check was positive set device into standby
	if (d->state == Standby_Check && !response) {
		xdebug("FRONIUS standby check positive for %s, delta load expected %d actual %d --> entering standby", d->name, d->dload, state->dload);
		(d->set_function)(d, 0);
		d->state = Standby;
		d->dload = 0;
		return;
	}

	// last delta load was too small (minimum 5%)
	int min = d->load * 5 / 100;
	if (abs(d->dload) < min) {
		xdebug("FRONIUS skipping standby check for %s, delta power only %d required %d", d->name, abs(d->dload), min);
		return;
	}

	// distortion - load values are not reliable
	if (state->distortion) {
		xdebug("FRONIUS skipping standby check for %s due to distortion %d", d->name, state->distortion);
		return;
	}

	// initiate a standby check
	xdebug("FRONIUS no response from %s, requesting standby check", d->name);
	d->state = Request_Standby_Check;
}

static void calculate_state() {
	// get 3x history back
	state_t *h1 = get_history(-1);
	state_t *h2 = get_history(-2);
	state_t *h3 = get_history(-3);

	// for validation
	state->sum = state->grid + state->akku + state->load + state->pv10;

	// total pv from both inverters
	state->pv = state->pv10 + state->pv7;
	state->dpv = state->pv - h1->pv;

	// subtract PV produced by Fronius7
	state->load -= state->pv7;

	// calculate delta load
	state->dload = state->load - h1->load;
	if (abs(state->dload) < NOISE)
		state->dload = 0;

	// wasting akku->grid power?
	if (state->akku > NOISE && state->grid < NOISE * -1) {
		int g = abs(state->grid);
		state->waste = g < state->akku ? g : state->akku;
	}

	// pv average / variation
	int avg = 0;
	unsigned long var = 0;
	for (int i = 0; i < HISTORY; i++) {
		state_t *h = get_history(i);
		avg += h->pv;
		var += abs(h->dpv);
	}
	avg /= HISTORY;
	var /= HISTORY;

	// grade of alternation in pv production when its cloudy with sunny gaps
	state->distortion = var * 10 / avg;

	// pv tendence
	if (h3->dpv < 0 && h2->dpv < 0 && h1->dpv < 0 && state->dpv < 0)
		state->tendence = -1; // pv is continuously falling
	else if (h3->dpv > 0 && h2->dpv > 0 && h1->dpv > 0 && state->dpv > 0)
		state->tendence = 1; // pv is continuously raising
	else
		state->tendence = 0;

	// allow more tolerance for bigger pv production
	int tolerance = state->pv > 2000 ? state->pv / 1000 : 1;
	int kf = KEEP_FROM * tolerance;
	int kt = KEEP_TO * tolerance;

	// grid < 0	--> upload
	// grid > 0	--> download

	// akku < 0	--> charge
	// akku > 0	--> discharge

	// surplus is akku charge + grid upload
	state->surplus = (state->grid + state->akku) * -1;

	// calculate greedy and modest power
	if (state->surplus > kt)
		state->greedy = state->surplus - kt;
	else if (state->surplus < kf)
		state->greedy = state->surplus - kf;
	else
		state->greedy = 0;

	// only grid load - not going into akku or from secondary inverters
	state->modest = state->greedy - abs(state->akku);

	// steal power from modest ramped adjustable devices for greedy dumb devices
	steal_power();

	char message[128];
	snprintf(message, 128, "avg:%d var:%lu kf:%d kt:%d", avg, var, kf, kt);
	print_power_status(message);
}

static int calculate_next_round(device_t *d) {
	// device ramp - wait for response
	if (d)
		return WAIT_NEXT;

	// determine wait for next round
	// much faster next round on
	// - extreme distortion
	// - wasting akku->grid power
	// - suspicious values from Fronius API
	// - big akku / grid load
	if (state->distortion > 3 || state->waste || state->sum > 250 || state->grid > 500 || state->akku > 500)
		return WAIT_NEXT;

	// all devices in standby?
	int all_standby = 1;
	for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++)
		if ((*ds)->device->state == Active)
			all_standby = 0;
	if (all_standby)
		return WAIT_STANDBY;

	// state is stable when we had no power change now and within last 3 rounds
	int instable = state->dload;
	for (int i = 1; i <= 3; i++)
		instable += get_history(i * -1)->dload;
	if (instable)
		return WAIT_IDLE;

	// state is stable, but faster next round when we have distortion
	if (state->distortion)
		return WAIT_STABLE / 2;
	else
		return WAIT_STABLE;
}

static void fronius() {
	int ret, wait = 1, errors = 0;
	device_t *device = 0;
	time_t next_reset;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// initializing
	init_all_devices();
	set_all_devices(0);
	ZERO(history);

	CURL *curl10 = curl_init(URL_FLOW10, callback_fronius10);
	if (curl10 == NULL) {
		xlog("Error initializing libcurl");
		return;
	}

	CURL *curl7 = curl_init(URL_FLOW7, callback_fronius7);
	if (curl7 == NULL) {
		xlog("Error initializing libcurl");
		return;
	}

	// the FRONIUS main loop
	while (1) {

		sleep(1);
		if (wait--)
			continue;

		// clear slot in history for storing new state
		state = &history[history_ptr];
		ZERO(state);

		// check error counter
		if (errors == 3)
			set_all_devices(0);

		// get actual calendar time
		time_t now_ts = time(NULL);
		now = localtime(&now_ts);

		// make Fronius10 API call
		ret = curl_perform(curl10);
		if (ret != 0) {
			xlog("FRONIUS Error calling Fronius10 API: %d", ret);
			errors++;
			wait = WAIT_NEXT;
			continue;
		}

		// not enough PV production, go into offline mode
		if (state->pv10 < 100) {
			print_power_status("--> offline");
			set_all_devices(0);
			wait = WAIT_OFFLINE;
			state->pv7 = 0;
			continue;
		}

		// reset program of the day and standby states every 30min
		if (potd == NULL || now_ts > next_reset) {
			mosmix();

			xlog("FRONIUS resetting standby and thermostat states");
			for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++) {
				device_t *d = (*ds)->device;
				d->state = Active;
				if (d->thermostat)
					d->power = 0;
			}

			next_reset = now_ts + STANDBY_RESET;
			wait = 1;
			continue;
		}

		// make Fronius7 API call
		ret = curl_perform(curl7);
		if (ret != 0)
			xlog("FRONIUS Error calling Fronius7 API: %d", ret);

		// calculate actual state
		calculate_state();

		// check response from previous ramp or do overall standby check
		if (device)
			check_response(device);
		else
			check_standby();

		// ramp up/down devices depending on if we have surplus or not
		device = 0;
		if (state->greedy)
			device = ramp();

		// determine wait for next round
		wait = state->wait = calculate_next_round(device);

		// set history pointer to next slot
		dump_history(6);
		if (++history_ptr == HISTORY)
			history_ptr = 0;

		print_device_status(wait, next_reset - now_ts);
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
	int voltage, closest, target, offset_start = 0, offset_end = 0;
	int measure[1000], raster[101];

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	// write IP and port into sockaddr structure
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	CURL *curl = curl_init(URL_METER, callback_calibrate);
	if (curl == NULL)
		perror("Error initializing libcurl");

	printf("starting calibration on %s (%s)\n", name, addr);
	snprintf(message, 16, "v:0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// average offset power at start
	printf("calculating offset start");
	for (int i = 0; i < 10; i++) {
		curl_easy_perform(curl);
		offset_start += state->grid;
		printf(" %d", state->grid);
		sleep(1);
	}
	offset_start /= 10;
	printf(" --> average %d\n", offset_start);

	printf("waiting for heat up 100%%...\n");
	snprintf(message, 16, "v:10000:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// get maximum power
	curl_easy_perform(curl);
	int max_power = round100(state->grid - offset_start);

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

		curl_easy_perform(curl);
		measure[i] = state->grid - offset_start;
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
		curl_easy_perform(curl);
		offset_end += state->grid;
		printf(" %d", state->grid);
		sleep(1);
	}
	offset_end /= 10;
	printf(" --> average %d\n", offset_end);

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
	device_t *d = &boiler1;

	d->power = -1;
	d->addr = resolve_ip(d->name);

	// 2 x blink
	set_boiler(d, 100);
	sleep(1);
	set_boiler(d, 0);
	sleep(1);
	set_boiler(d, 100);
	sleep(1);
	set_boiler(d, 0);
	sleep(1);

	// full ramp up 0..100
	for (int i = 0; i <= 100; i++) {
		set_boiler(d, i);
		usleep(200 * 1000);
	}
	set_boiler(d, 0);
	return 0;
}

static int init() {
	set_debug(1);
	return 0;
}

static void stop() {
	if (sock != 0)
		close(sock);
}

int fronius_override(const char *name) {
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = devices[i];
		if (!strcmp(d->name, name)) {
			xlog("FRONIUS Activating Override for %d seconds on %s", OVERRIDE, d->name);
			d->override = time(NULL) + OVERRIDE;
			d->state = Active;
			(d->set_function)(d, 100);
		}
	}
	return 0;
}

int fronius_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	// no arguments - main loop
	if (argc == 1) {
		init();
		pause();
		stop();
		return 0;
	}

	init_all_devices();

	int c;
	while ((c = getopt(argc, argv, "c:o:t")) != -1) {
		switch (c) {
		case 'c':
			// execute as: stdbuf -i0 -o0 -e0 ./fronius -c boiler1 > boiler1.txt
			return calibrate(optarg);
		case 'o':
			return fronius_override(optarg);
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
