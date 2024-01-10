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

static pthread_t thread;
static int sock = 0;
static int wait = 3;
static int standby_timer;
static get_request_t req = { .buffer = NULL, .len = 0, .buflen = CHUNK_SIZE };
static CURL *curl;

// boiler status
const char *boilers[] = { BOILERS };
static boiler_t **boiler;

static size_t callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
	size_t realsize = size * nmemb;
	get_request_t *req = (get_request_t*) userdata;

	if (req->buffer == NULL)
		req->buffer = malloc(CHUNK_SIZE);

	while (req->buflen < req->len + realsize + 1) {
		req->buffer = realloc(req->buffer, req->buflen + CHUNK_SIZE);
		req->buflen += CHUNK_SIZE;
	}

	memcpy(&req->buffer[req->len], ptr, realsize);
	req->len += realsize;
	req->buffer[req->len] = 0;

	return realsize;
}

static void print_status() {
	char message[32];
	char value[5];

	strcpy(message, "FRONIUS boiler");
	for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
		snprintf(value, 5, " %3d", boiler[i]->power);
		strcat(message, value);
	}

	snprintf(value, 5, "%3d", wait);
	strcat(message, "   wait ");
	strcat(message, value);
	xlog(message);
}

static int check_all(int value) {
	int check = 1;
	for (int i = 0; i < ARRAY_SIZE(boilers); i++)
		if (boiler[i]->power != value)
			check = 0;
	return check;
}

// Grid < 0	--> upload
// Grid > 0	--> download

// Akku < 0	--> charge
// Akku > 0	--> discharge

// 100% == 2000 watt --> 1% == 20W
static int calculate_step(int akku, int grid, int load, int pv) {
	// allow 100 watt upload
	int surplus = ((grid - 100) + akku) * -1;

	int step;
	if (surplus > 1000)
		// big surplus - normal steps
		step = surplus / 20;
	else if (surplus > 0)
		// small surplus - smaller steps
		step = surplus / 20 / 2;
	else
		// overload - normal steps
		step = surplus / 20;

	if (step < -100)
		step = -100; // max -100

	if (step > 100)
		step = 100; // max 100

	if (step < 0)
		xlog("FRONIUS surplus:%d rampdown step:%d", surplus, step);
	else if (step > 0)
		xlog("FRONIUS surplus:%d rampup step:%d", surplus, step);

	return step;
}

static int set_boiler(boiler_t *boiler, int power) {
	if (power < 0)
		power = 0;

	if (power > 100)
		power = 100;

	boiler->power = power;

	if (boiler->addr == NULL)
		return -1;

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
	char message[16];
	unsigned int voltage = power == 0 ? 0 : power * 80 + 2000;
	snprintf(message, 16, "%d:%d", voltage, 0);

	// send message to boiler
	if (sendto(sock, message, strlen(message), 0, sa, sizeof(*sa)) < 0)
		return xerr("Sendto failed");

	return 0;
}

static void set_boilers(int power) {
	for (int i = 0; i < ARRAY_SIZE(boilers); i++)
		set_boiler(boiler[i], power);
}

static void keep() {
	wait = WAIT_KEEP;
}

static void offline() {
	wait = WAIT_OFFLINE;

	if (check_all(0)) {
		xlog("FRONIUS offline");
		return;
	}

	xlog("FRONIUS entering offline");
	set_boilers(0);
}

static void rampup(int step, int akku, int grid, int load) {
	// check if all boilers are in standby mode
	if (check_all(BOILER_STANDBY)) {
		if (--standby_timer == 0 || abs(load) > 500) {
			// exit standby once per hour or when load > 0,5kW
			set_boilers(0);
			wait = WAIT_KEEP;
			xlog("FRONIUS exiting standby");
			return;
		}

		wait = WAIT_STANDBY;
		xlog("FRONIUS rampup standby %d", standby_timer);
		return;
	}

	// check if all boilers are ramped up to 100% but do not consume power
	if (check_all(100) && (abs(load) < 500)) {
		set_boilers(BOILER_STANDBY);
		wait = WAIT_STANDBY;
		standby_timer = STANDBY_EXPIRE;
		xlog("FRONIUS entering standby");
		return;
	}

	wait = WAIT_RAMPUP;

	// rampup each boiler separately
	for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
		if (boiler[i]->power != 100) {
			int boiler_power = boiler[i]->power;
			boiler_power += step;
			if (boiler_power == BOILER_STANDBY)
				boiler_power++; // not the standby value
			set_boiler(boiler[i], boiler_power);
			return;
		}
	}
}

static void rampdown(int step, int akku, int grid, int load) {
	// check if all boilers are ramped down
	if (check_all(0)) {
		wait = WAIT_STANDBY;
		xlog("FRONIUS rampdown standby");
		return;
	}

	wait = WAIT_RAMPDOWN;

	// lowering all boilers
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

	// initialize all boilers
	set_boilers(0);

	float p_akku, p_grid, p_load, p_pv;
	int akku, grid, load, pv;

	while (1) {
		sleep(wait);

		// Idee: if afternoon() && PV < 1000 then boiler[3]->active = 0
		// wenn er bis dahin nicht voll ist - pech gehabt - rest geht in Akku

		for (int i = 0; i < ARRAY_SIZE(boilers); i++)
			if (boiler[i]->override)
				set_boiler(boiler[i], 100);

		req.len = 0;
		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
			xerr("FRONIUS No response from Fronius API: %d", res);
			offline();
			continue;
		}

		// printf("Received data:/n%s\n", req.buffer);
		// json_scanf(req.buffer, req.len, "{ Body { Data { PowerReal_P_Sum:%f } } }", &grid_power);
		// printf("Grid Power %f\n", grid_power);

		json_scanf(req.buffer, req.len, "{ Body { Data { Site { P_Akku:%f, P_Grid:%f, P_Load:%f, P_PV:%f } } } }", &p_akku, &p_grid, &p_load, &p_pv);
		akku = p_akku;
		grid = p_grid;
		load = p_load;
		pv = p_pv;
		xlog("FRONIUS Akku:%d, Grid:%d, Load:%d, PV:%d", akku, grid, load, pv);

		int step = calculate_step(akku, grid, load, pv);

		if (pv == 0)
			// no PV production, go into offline mode
			offline();
		else if (step < 0)
			// consuming grid power: ramp down
			rampdown(step, akku, grid, load);
		else if (step == 0)
			// uploading grid power between 0 and 100 watts: keep current state
			keep();
		else if (step > 0)
			// uploading grid power: ramp up
			rampup(step, akku, grid, load);

		print_status();
	}
}

static int init() {
	boiler = malloc(ARRAY_SIZE(boilers));
	for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
		boiler_t *b = malloc(sizeof(boiler_t));
		b->name = boilers[i];
		b->addr = resolve_ip(b->name);
		b->active = 1;
		b->override = 0;
		b->power = 0;
		boiler[i] = b;
	}

	curl = curl_easy_init();
	if (curl == NULL)
		return xerr("Error initializing libcurl");

	curl_easy_setopt(curl, CURLOPT_URL, URL);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "http");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void* ) &req);

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
	free(req.buffer);
	curl_easy_cleanup(curl);

	if (sock != 0)
		close(sock);
}

void fronius_override(int index) {
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
MCP_REGISTER(fronius, 1, &init, &stop);
#endif

