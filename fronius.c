#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <curl/curl.h>

#include "fronius.h"
#include "frozen.h"
#include "utils.h"
#include "mcp.h"

#define FRONIUSLOG 			"FRONIUS Charge:%5d Akku:%5d Grid:%5d Load:%5d PV:%5d Surplus:%5d Step:%3d"

static pthread_t thread;

// curl response buffer
static CURL *curl;
static get_response_t res = { .buffer = NULL, .len = 0, .buflen = CHUNK_SIZE };

// boiler status
static const char *boilers[] = { BOILERS };
static device_t **boiler;

// heater status
static const char *heaters[] = { HEATERS };
static device_t **heater;

// actual Fronius power flow data + calculations
static int charge, akku, grid, load, pv, surplus, step;

// PV history values to calculate distortion
static int pv_history[PV_HISTORY];
static int pv_history_ptr = 0;

// SSR control voltage for 0..100% power
static const unsigned int phase_angle[] = { PHASE_ANGLES };

static int sock = 0;
static int wait = 3;
static int standby_timer;

static size_t callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
	size_t realsize = size * nmemb;
	get_response_t *xres = (get_response_t*) userdata;

	if (xres->buffer == NULL)
		xres->buffer = malloc(CHUNK_SIZE);

	while (xres->buflen < xres->len + realsize + 1) {
		xres->buffer = realloc(xres->buffer, xres->buflen + CHUNK_SIZE);
		xres->buflen += CHUNK_SIZE;
	}

	memcpy(&xres->buffer[xres->len], ptr, realsize);
	xres->len += realsize;
	xres->buffer[xres->len] = 0;

	return realsize;
}

static void parse() {
	float p_charge, p_akku, p_grid, p_load, p_pv;

	// printf("Received data:/n%s\n", req.buffer);
	// json_scanf(req.buffer, req.len, "{ Body { Data { PowerReal_P_Sum:%f } } }", &grid_power);
	// printf("Grid Power %f\n", grid_power);

	json_scanf(res.buffer, res.len, "{ Body { Data { Site { P_Akku:%f, P_Grid:%f, P_Load:%f, P_PV:%f } } } }", &p_akku, &p_grid, &p_load, &p_pv);

	// workaround parsing { "Inverters" : { "1" : { ... } } }
	char *c, *p;
	json_scanf(res.buffer, res.len, "{ Body { Data { Inverters:%Q } } }", &c);
	p = c;
	while (*p != '{')
		p++;
	p++;
	while (*p != '{')
		p++;
	json_scanf(p, strlen(p) - 1, "{ SOC:%f }", &p_charge);
	free(c);

	charge = p_charge;
	akku = p_akku;
	grid = p_grid;
	load = p_load;
	pv = p_pv;
}

static int set_heater(device_t *heater, int power) {
	if (heater->power == power)
		return 0;

	if (heater->addr == NULL)
		return -1;

	heater->power = power;

	char command[128];
	if (power) {
		xlog("FRONIUS switching %s ON", heater->name);
		snprintf(command, 128, "curl --silent --output /dev/null http://%s/cm?cmnd=Power%%20On", heater->addr);
	} else {
		xlog("FRONIUS switching %s OFF", heater->name);
		snprintf(command, 128, "curl --silent --output /dev/null http://%s/cm?cmnd=Power%%20Off", heater->addr);
	}

	system(command);
	return 0;
}

static int check_all_heaters(int value) {
	int check = 1;
	for (int i = 0; i < ARRAY_SIZE(heaters); i++)
		if (heater[i]->power != value)
			check = 0;
	return check;
}

static void set_heaters(int power) {
	for (int i = 0; i < ARRAY_SIZE(heaters); i++)
		set_heater(heater[i], power);
}

static int set_boiler(device_t *boiler, int power) {
	if (boiler->power == power)
		return 0;

	if (boiler->addr == NULL)
		return -1;

	if (power < 0)
		power = 0;

	if (power > 100)
		power = 100;

	boiler->power = power;

	// boiler is not active - completely switch off
	if (!boiler->active)
		power = 0;

	// countdown override and set power to 100%
	if (boiler->override) {
		boiler->override--;
		power = 100;
		xlog("FRONIUS Override active for %s remaining %d loops", boiler->name, boiler->override);
	}

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock == 0)
		return xerr("Error creating socket");

	// update IP and port in sockaddr structure
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(boiler->addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	// convert 0..100% to 2..10V SSR control voltage
	// int voltage = power == 0 ? 0 : power * 80 + 2000;
	int voltage = phase_angle[power];
	char message[16];
	snprintf(message, 16, "%d:%d", voltage, 0);

	// send message to boiler
	xlog("FRONIUS send %s UDP %s", boiler->name, message);
	if (sendto(sock, message, strlen(message), 0, sa, sizeof(*sa)) < 0)
		return xerr("Sendto failed");

	return 0;
}

static void set_boilers(int power) {
	for (int i = 0; i < ARRAY_SIZE(boilers); i++)
		set_boiler(boiler[i], power);
}

static int check_all_boilers(int value) {
	int check = 1;
	for (int i = 0; i < ARRAY_SIZE(boilers); i++)
		if (boiler[i]->power != value)
			check = 0;
	return check;
}

static void print_status() {
	char message[128];
	char value[5];

	strcpy(message, "FRONIUS boilers active ");
	for (int i = 0; i < ARRAY_SIZE(boilers); i++)
		strcat(message, boiler[i]->active ? "1" : "0");

	strcat(message, "   power ");
	for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
		snprintf(value, 5, " %3d", boiler[i]->power);
		strcat(message, value);
	}

	strcat(message, "   heaters active ");
	for (int i = 0; i < ARRAY_SIZE(heaters); i++)
		strcat(message, heater[i]->active ? "1" : "0");

	strcat(message, "   power ");
	for (int i = 0; i < ARRAY_SIZE(heaters); i++)
		strcat(message, heater[i]->power ? "1" : "0");

	snprintf(value, 5, "%3d", wait);
	strcat(message, "   wait ");
	strcat(message, value);
	xlog(message);
}

// if cloudy then we have alternating lighting conditions and therefore big distortion in PV production
static int calculate_pv_distortion() {
	char message[128];
	char value[8];

	strcpy(message, "[");
	for (int i = 0; i < PV_HISTORY; i++) {
		snprintf(value, 8, "%d", pv_history[i]);
		if (i > 0 && i < PV_HISTORY)
			strcat(message, ", ");
		strcat(message, value);
	}
	strcat(message, "]");

	int average = 0;
	for (int i = 0; i < PV_HISTORY; i++)
		average += pv_history[i];
	average /= PV_HISTORY;

	int variation = 0;
	for (int i = 0; i < PV_HISTORY; i++)
		variation += abs(average - pv_history[i]);
	variation /= PV_HISTORY;

	int diff = pv - average;
	int distortion = abs(diff) > variation;
	xlog("FRONIUS calculate_pv_distortion() %s average:%d diff:%d variation:%d --> distortion:%d", message, average, diff, variation, distortion);

	return distortion;
}

// Grid < 0	--> upload
// Grid > 0	--> download

// Akku < 0	--> charge
// Akku > 0	--> discharge

// 100% == 2000 watt --> 1% == 20W
static void calculate_step() {
	// allow 100 watt grid upload or akku charging
	// surplus = (grid + akku) * -1 - 100;

	// to avoid swinging if surplus is very small
	surplus = (grid + akku) * -1;
	if (0 < surplus && surplus < 50) {
		step = 0;
		return;
	} else if (50 <= surplus && surplus < 100) {
		step = 1;
		return;
	}

	int distortion = calculate_pv_distortion();
	int onepercent = BOILER_WATT / 100;

	if (surplus > BOILER_WATT / 2)
		// big surplus - normal steps
		step = surplus / onepercent;
	else if (surplus < 0)
		// overload - normal steps
		step = surplus / onepercent;
	else {
		if (distortion)
			// small surplus and big distortion - very small steps
			step = surplus / onepercent / 2 / 2;
		else
			// small surplus - small steps
			step = surplus / onepercent / 2;
	}

	if (step < -100)
		step = -100; // min -100

	if (step > 100)
		step = 100; // max 100
}

static void offline() {
	step = 0;
	surplus = 0;
	wait = WAIT_OFFLINE;
	xlog(FRONIUSLOG" --> offline", charge, akku, grid, load, pv, surplus, step);

	if (check_all_boilers(0) && check_all_heaters(0))
		return;

	set_heaters(0);
	set_boilers(0);
}

static void keep() {
	wait = WAIT_KEEP;
	xlog(FRONIUSLOG" --> keep", charge, akku, grid, load, pv, surplus, step);
}

static void rampup() {
	// exit standby once per hour
	if (standby_timer) {
		if (--standby_timer == 0) {
			set_heaters(0);
			set_boilers(0);
			wait = 0;
			xlog("FRONIUS exiting standby");
			return;
		}
	}

	// check if all boilers are in standby and all heaters on
	if (check_all_boilers(BOILER_STANDBY) && check_all_heaters(1)) {
		wait = WAIT_STANDBY;
		xlog(FRONIUSLOG" --> ramp↑ standby %d", charge, akku, grid, load, pv, surplus, step, standby_timer);
		return;
	}

	// check if all boilers are ramped up to 100% but do not consume power
	if (check_all_boilers(100) && (abs(load) < (BOILER_WATT / 2))) {
		set_boilers(BOILER_STANDBY);
		standby_timer = STANDBY_EXPIRE;
		wait = WAIT_STANDBY;
		xlog("FRONIUS entering standby");
		return;
	}

	wait = WAIT_RAMPUP;
	xlog(FRONIUSLOG" --> ramp↑", charge, akku, grid, load, pv, surplus, step);

	// rampup each boiler separately
	for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
		if (boiler[i]->power != 100 && boiler[i]->power != BOILER_STANDBY) {
			int boiler_power = boiler[i]->power;
			boiler_power += step;
			if (boiler_power == BOILER_STANDBY)
				boiler_power++; // not the standby value
			set_boiler(boiler[i], boiler_power);
			return;
		}
	}

	// check if we have enough surplus for at least one heater
	if (surplus < HEATER_WATT)
		return;

	// switch on heater only when cold
//	if (sensors->bmp085_temp > 15)
//		return;

	// switch on each heater separately
	for (int i = 0; i < ARRAY_SIZE(heaters); i++)
		if (!heater[i]->power) {
			set_heater(heater[i], 1);
			return;
		}
}

static void rampdown() {
	// check if all heaters and boilers are ramped down
	if (check_all_boilers(0) && check_all_heaters(0)) {
		wait = WAIT_STANDBY;
		xlog(FRONIUSLOG" --> ramp↓ standby", charge, akku, grid, load, pv, surplus, step);
		return;
	}

	wait = WAIT_RAMPDOWN;
	xlog(FRONIUSLOG" --> ramp↓", charge, akku, grid, load, pv, surplus, step);

	// first switch off heaters separately
	for (int i = ARRAY_SIZE(heaters) - 1; i >= 0; i--)
		if (heater[i]->power) {
			set_heater(heater[i], 0);
			return;
		}

	// then lowering all boilers - as we don't know which one consumes power
	for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
		if (boiler[i]->power) {
			int boiler_power = boiler[i]->power;
			boiler_power += step;
			if (boiler_power == BOILER_STANDBY)
				boiler_power--; // not the standby value
			set_boiler(boiler[i], boiler_power);
		}
	}
}

static void* fronius(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	// switch off all
	set_boilers(0);
	set_heaters(0);

	while (1) {

		sleep(1);
		if (wait--)
			continue;

		// enable boiler2+3 if akku charge is greater than 75% or pv more than boiler power
		if (charge > 75 || pv > BOILER_WATT) {
			boiler[1]->active = 1;
			boiler[2]->active = 1;
		} else {
			boiler[1]->active = 0;
			boiler[2]->active = 0;
		}

		// check if override is active
		for (int i = 0; i < ARRAY_SIZE(boilers); i++)
			if (boiler[i]->override) {
				boiler[i]->active = 1;
				set_boiler(boiler[i], 100);
			}

		// make Fronius API call
		res.len = 0;
		CURLcode ret = curl_easy_perform(curl);
		if (ret != CURLE_OK) {
			xerr("FRONIUS No response from Fronius API: %d", ret);
			wait = WAIT_KEEP;
			set_boilers(0);
			continue;
		}

		// extract values from Fronius JSON response
		parse();

		// not enough PV production, go into offline mode
		if (pv < 100) {
			offline();
			continue;
		}

		// update PV history
		pv_history[pv_history_ptr++] = pv;
		if (pv_history_ptr == PV_HISTORY)
			pv_history_ptr = 0;

		calculate_step();
		if (step < 0)
			// consuming grid power or discharging akku: ramp down
			rampdown();
		else if (step > 0)
			// uploading grid power or charging akku: ramp up
			rampup();
		else
			// keep current state
			keep();

		print_status();
	}
}

static int init() {
	// create boiler device structures
	boiler = malloc(ARRAY_SIZE(boilers));
	for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
		device_t *b = malloc(sizeof(device_t));
		b->name = boilers[i];
		b->addr = resolve_ip(b->name);
		b->active = 1;
		b->override = 0;
		b->power = -1;
		boiler[i] = b;
	}

	// create heater device structures
	heater = malloc(ARRAY_SIZE(heaters));
	for (int i = 0; i < ARRAY_SIZE(heaters); i++) {
		device_t *h = malloc(sizeof(device_t));
		h->name = heaters[i];
		h->addr = resolve_ip(h->name);
		h->active = 1;
		h->override = 0;
		h->power = -1;
		heater[i] = h;
	}

	curl = curl_easy_init();
	if (curl == NULL)
		return xerr("Error initializing libcurl");

	curl_easy_setopt(curl, CURLOPT_URL, URL);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "http");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void* ) &res);

	if (pthread_create(&thread, NULL, &fronius, NULL))
		return xerr("Error creating fronius thread");

	xlog("FRONIUS initialized");
	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("Error canceling fronius thread");

	if (pthread_join(thread, NULL))
		xlog("Error joining fronius thread");

	// stop and destroy this module
	free(res.buffer);
	curl_easy_cleanup(curl);

	if (sock != 0)
		close(sock);
}

void fronius_override(int index) {
	if (index < 0 || index >= ARRAY_SIZE(boilers))
		return;

	wait = 0; // immediately exit wait loop
	boiler[index]->override = 10; // 10 x WAIT_OFFLINE -> 10 minutes
	xlog("FRONIUS Setting Override for %s loops %d", boiler[index]->name, boiler[index]->override);
}

int fronius_main(int argc, char **argv) {
	init();

	pause();

	stop();
	return 0;
}

#ifdef FRONIUS_MAIN
int main(int argc, char **argv) {
	return fronius_main(argc, argv);
}
#else
MCP_REGISTER(fronius, 7, &init, &stop);
#endif

