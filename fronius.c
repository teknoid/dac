#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <curl/curl.h>

#include "fronius.h"
#include "frozen.h"
#include "utils.h"
#include "mcp.h"

static pthread_t thread;
static int wait = 3;

static CURL *curl;
static get_request_t req = { .buffer = NULL, .len = 0, .buflen = CHUNK_SIZE };

static int boiler[] = { 0, 0, 0 };

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
	printf("boiler");
	for (int i = 0; i < ARRAY_SIZE(boiler); i++)
		printf(" %3d", boiler[i]);

	printf("   wait %d\n", wait);
}

static void set_boiler(int id) {
	char command[128];
	// convert 0..100% to 2..10V SSR control voltage 
	int voltage = boiler[id] == 0 ? 0 : boiler[id] * 80 + 2000;
	snprintf(command, 128, "curl --silent --output /dev/null -X POST http://boiler%d/0/%d", id + 1, voltage);
	system(command);
}

static void deny(int new_wait) {
	printf("deny\n");
	wait = new_wait;

	for (int i = 0; i < ARRAY_SIZE(boiler); i++)
		if (boiler[i]) {
			boiler[i] = 0;
			set_boiler(i);
		}
}

static void rampup(int new_wait, float p_grid, float p_load) {
	wait = new_wait;

	// check if all boilers are in standby mode
	int standby = 1;
	for (int i = 0; i < ARRAY_SIZE(boiler); i++)
		if (boiler[i] != BOILER_STANDBY)
			standby = 0;
	if (standby) {
		wait = 30;
		printf("standby\n");
		return;
	}

	// check if all boilers are ramped up to 100% but do not consume power
	int full = 1;
	for (int i = 0; i < ARRAY_SIZE(boiler); i++)
		if (boiler[i] != 100)
			full = 0;
	int load = abs(p_load);
	if (full && (load < 1000)) {
		for (int i = 0; i < ARRAY_SIZE(boiler); i++) {
			boiler[i] = BOILER_STANDBY;
			set_boiler(i);
		}
		wait = 30;
		printf("entering standby\n");
		return;
	}

	// 100% == 2000 watt
	int surplus = abs(p_grid) - 100;
	int step = surplus / 50;
	printf("rampup step %d\n", step);

	// a boiler consumes 0 to 100% in steps depending of surplus power
	for (int i = 0; i < ARRAY_SIZE(boiler); i++) {
		if (boiler[i] != 100) {
			boiler[i] += step;
			if (boiler[i] > 100)
				boiler[i] = 100;
			set_boiler(i);
			return;
		}
	}
}

static void rampdown(int new_wait, float p_grid, float p_load) {
	wait = new_wait;

	// check if all boilers are ramped down
	int off = 1;
	for (int i = 0; i < ARRAY_SIZE(boiler); i++)
		if (boiler[i] != 0)
			off = 0;
	if (off) {
		wait = 30;
		printf("standby\n");
		return;
	}

	// 100% == 2000 watt
	int surplus = abs(p_grid) - 100;
	int step = surplus / 50;
	printf("rampdown step %d\n", step);

	// lowering boilers
	for (int i = ARRAY_SIZE(boiler) - 1; i >= 0; i--) {
		if (boiler[i]) {
			boiler[i] -= step;
			if (boiler[i] < 0)
				boiler[i] = 0;
			set_boiler(i);
			return;
		}
	}
}

static void* fronius(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	float p_akku, p_grid, p_load, p_pv;

	while (1) {
		sleep(wait);

		req.len = 0;
		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
			xerr("No response from Fronius API");
			deny(60);
			continue;
		}

//		printf("Received data:/n%s\n", req.buffer);

//		json_scanf(req.buffer, req.len, "{ Body { Data { PowerReal_P_Sum:%f } } }", &grid_power);
//		printf("Grid Power %f\n", grid_power);

		json_scanf(req.buffer, req.len, "{ Body { Data { Site { P_Akku:%f, P_Grid:%f, P_Load:%f, P_PV:%f } } } }", &p_akku, &p_grid, &p_load, &p_pv);
		printf("P_Akku:%f, P_Grid:%f, P_Load:%f, P_PV:%f\n", p_akku, p_grid, p_load, p_pv);

		if (p_pv == 0)
			// no PV production, wait 60 seconds
			deny(60);
		else if (-100 < p_grid && p_grid < 100)
			// -100 to 100 watts: keep current state for 10 seconds
			wait = 10;
		else if (p_grid < -100)
			// upload over 100 watts: slowly ramp up
			rampup(3, p_grid, p_load);
		else if (p_grid > 100)
			// consume more than 100 watts: quickly rampdown
			rampdown(1, p_grid, p_load);

		print_status();
	}
}

static int init() {
	for (int i = 0; i < ARRAY_SIZE(boiler); i++)
		set_boiler(i);

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

	xlog("fronius initialized");
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

