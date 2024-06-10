#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>

#include "tasmota.h"
#include "flamingo.h"
#include "utils.h"
#include "xmas.h"
#include "mcp.h"

// these tasmota devices will be in XMAS mode
#ifndef PICAM
static const unsigned int device[] = { DEVICES };
#endif

// TODO define channel status for each remote control unit
static char channel_status[128];

static pthread_t thread;
static int power = -1;

void xmas_on() {
#ifndef PICAM
	for (int i = 0; i < ARRAY_SIZE(device); i++)
		tasmota_power(device[i], 0, 1);
#endif
}

void xmas_off() {
#ifndef PICAM
	for (int i = 0; i < ARRAY_SIZE(device); i++)
		tasmota_power(device[i], 0, 0);
#endif
}

static void xmas_on_flamingo(const timing_t *timing) {
	int index = timing->channel - 'A';
	if (!channel_status[index]) {
		xlog("flamingo_send_FA500 %d %c 1", timing->remote, timing->channel);
		flamingo_send_FA500(timing->remote, timing->channel, 1, -1);
		channel_status[index] = 1;
	}
}

static void xmas_off_flamingo(const timing_t *timing) {
	int index = timing->channel - 'A';
	if (channel_status[index]) {
		xlog("flamingo_send_FA500 %d %c 0", timing->remote, timing->channel);
		flamingo_send_FA500(timing->remote, timing->channel, 0, -1);
		channel_status[index] = 0;
	}
}

static void on_sundown(const timing_t *timing, uint16_t lumi) {
	xlog("XMAS reached SUNDOWN at %d", lumi);
	xmas_on();
	xmas_on_flamingo(timing);
	power = 1;
}

static void on_morning(const timing_t *timing) {
	xlog("XMAS reached ON time frame");
	xmas_on();
	xmas_on_flamingo(timing);
	power = 1;
}

static void off_sunrise(const timing_t *timing, uint16_t lumi) {
	xlog("XMAS reached SUNRISE at %d", lumi);
	xmas_off();
	xmas_off_flamingo(timing);
	power = 0;
}

static void off_evening(const timing_t *timing) {
	xlog("XMAS reached OFF time frame");
	xmas_off();
	xmas_off_flamingo(timing);
	power = 0;
}

static int process(struct tm *now, const timing_t *timing, unsigned int lumi) {
	int afternoon = now->tm_hour < 12 ? 0 : 1;
	int curr = now->tm_hour * 60 + now->tm_min;
	int from = timing->on_h * 60 + timing->on_m;
	int to = timing->off_h * 60 + timing->off_m;

	if (from <= curr && curr <= to) {
		// ON time frame

		// already on
		if (power == 1)
			return 0;

		if (afternoon) {
			// evening: check if sundown is reached an switch on
			if (lumi < SUNDOWN)
				on_sundown(timing, lumi);
			else
				// xlog("in ON time, waiting for XMAS_SUNDOWN(%d:%d) ", lumi, XMAS_SUNDOWN);
				return 0;
		} else
			// morning: switch on
			on_morning(timing);

	} else {
		// OFF time frame

		// already off
		if (power == 0)
			return 0;

		if (!afternoon)
			// morning: check if sunrise is reached an switch off
			if (lumi > SUNRISE)
				off_sunrise(timing, lumi);
			else
				// xlog("in OFF time, waiting for XMAS_SUNRISE(%d:%d)", lumi, XMAS_SUNRISE);
				return 0;
		else
			// evening: switch off
			off_evening(timing);
	}

	return 0;
}

static void* xmas(void *arg) {
	unsigned int lumi;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("XMAS Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	// elevate realtime priority for flamingo 433MHz transmit
	if (elevate_realtime(3) < 0) {
		xlog("XMAS Error elevating realtime");
		return (void*) 0;
	}

	sleep(1); // wait for sensors
	lumi = sensors->bh1750_lux;
	if (lumi == UINT16_MAX) {
		xlog("XMAS no sensor data");
		return (void*) 0;
	}

	while (1) {
		time_t now_ts = time(NULL);
		struct tm *now = localtime(&now_ts);

		for (int i = 0; i < ARRAY_SIZE(timings); i++) {
			const timing_t *timing = &timings[i];

			if (!timing->active)
				continue;

			if (now->tm_wday != timing->wday)
				continue;

			// xlog("processing timing[%i]", i);
			process(now, timing, lumi);
		}

		sleep(60);
	}
}

static int init() {
	if (pthread_create(&thread, NULL, &xmas, NULL))
		xlog("Error creating xmas thread");

	xlog("XMAS initialized");
	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("Error canceling xmas thread");

	if (pthread_join(thread, NULL))
		xlog("Error joining xmas thread");
}

MCP_REGISTER(xmas, 6, &init, &stop);
