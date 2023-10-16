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
static int wait = 1;

static CURL *curl;
static get_request_t req = { .buffer = NULL, .len = 0, .buflen = CHUNK_SIZE };

static int watteater[] = { 0, 0, 0 };
static int heater[] = { 0, 0 };

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
	printf("watteater");
	for (int i = 0; i < ARRAY_SIZE(watteater); i++)
		printf(" %3d", watteater[i]);
	printf("   ");

	printf("heater");
	for (int i = 0; i < ARRAY_SIZE(heater); i++)
		printf(" %d", heater[i]);
	printf("\n");
}

static void set_watteater(int id) {
}

static void set_heater(int id) {
}

static void deny(int new_wait) {
	printf("deny\n");
	wait = new_wait;

	for (int i = 0; i < ARRAY_SIZE(watteater); i++)
		if (watteater[i]) {
			watteater[i] = 0;
			set_watteater(i);
		}

	for (int i = 0; i < ARRAY_SIZE(heater); i++)
		if (heater[i]) {
			heater[i] = 0;
			set_heater(i);
		}
}

static void rampup(int new_wait, int surplus) {
	printf("rampup (Surplus Power: %d)\n", surplus);
	wait = new_wait;

	// 100 watt min surplus is 5% at 2000 watt max
	int step = surplus / 20;

	// a watteater consumes 0 to 100% in steps depending of surplus power
	for (int i = 0; i < ARRAY_SIZE(watteater); i++) {
		if (watteater[i] != 100) {
			watteater[i] += step;
			if (watteater[i] > 100)
				watteater[i] = 100;
			set_watteater(i);
			return;
		}
	}

	// a heater consumes HEATER_WATT at once
	if (surplus < HEATER_WATT)
		return;

	// heater only when cold
//	if (sensors->bmp085_temp > 15)
//		return;

	for (int i = 0; i < ARRAY_SIZE(heater); i++)
		if (heater[i] != 1) {
			heater[i] = 1;
			set_heater(i);
			return;
		}
}

static void rampdown(int new_wait, int lower) {
	printf("rampdown (Lowering Power: %d)\n", lower);
	wait = new_wait;

	// first switch off heaters
	for (int i = ARRAY_SIZE(heater) - 1; i >= 0; i--)
		if (heater[i]) {
			heater[i] = 0;
			set_heater(i);
			return;
		}

	// then lowering watteaters
	for (int i = ARRAY_SIZE(watteater) - 1; i >= 0; i--) {
		if (watteater[i]) {
			watteater[i] -= 10;
			if (watteater[i] < 0)
				watteater[i] = 0;
			set_watteater(i);
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
			// no PV production, wait 60 seconds and evaluate new
			deny(60);
		else if (p_grid >= 0)
			// not enough PV production, wait 30 seconds and evaluate new
			deny(30);
		else if (p_grid < 0 && p_grid > -100)
			// 0 to 100 watts: quickly shut down bulk consumers
			rampdown(1, (int) (0 - p_grid));
		else if (p_grid < -100)
			// over 100 watts: slowly ramp up bulk consumers
			rampup(10, (int) (0 - p_grid - 100));

		print_status();
	}
}

static int init() {
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

