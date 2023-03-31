#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>

#include "shelly.h"
#include "utils.h"
#include "xmas.h"
#include "mqtt.h"
#include "mcp.h"

// these shellies will be in XMAS mode
static const unsigned int SHELLIES[] = { PLUG1, PLUG2 };

static pthread_t thread;
static int power = -1;

static void on() {
	for (int i = 0; i < ARRAY_SIZE(SHELLIES); i++)
		shelly_command(SHELLIES[i], 1);
	power = 1;
}

static void off() {
	for (int i = 0; i < ARRAY_SIZE(SHELLIES); i++)
		shelly_command(SHELLIES[i], 0);
	power = 0;
}

static void on_sundown() {
	xlog("XMAS reached SUNDOWN at %d", sensors->bh1750_lux);
	on();
}

static void on_morning() {
	xlog("XMAS reached ON time frame");
	on();
}

static void off_sunrise() {
	xlog("XMAS reached SUNRISE at %d", sensors->bh1750_lux);
	off();
}

static void off_evening() {
	xlog("XMAS reached OFF time frame");
	off();
}

static int process(struct tm *now, const timing_t *timing) {
	int lumi = sensors->bh1750_lux;
	int afternoon = now->tm_hour < 12 ? 0 : 1;
	int curr = now->tm_hour * 60 + now->tm_min;
	int from = timing->on_h * 60 + timing->on_m;
	int to = timing->off_h * 60 + timing->off_m;

	if (lumi == INT_MAX)
		return xerr("XMAS no sensor data");

	if (from <= curr && curr <= to) {
		// ON time frame

		// already on
		if (power == 1)
			return 0;

		if (afternoon) {
			// evening: check if sundown is reached an switch on
			if (lumi < XMAS_SUNDOWN)
				on_sundown();
			else
				// xlog("in ON time, waiting for XMAS_SUNDOWN(%d:%d) ", lumi, XMAS_SUNDOWN);
				return 0;
		} else
			// morning: switch on
			on_morning();

	} else {
		// OFF time frame

		// already off
		if (power == 0)
			return 0;

		if (!afternoon)
			// morning: check if sunrise is reached an switch off
			if (lumi > XMAS_SUNRISE)
				off_sunrise();
			else
				// xlog("in OFF time, waiting for XMAS_SUNRISE(%d:%d)", lumi, XMAS_SUNRISE);
				return 0;
		else
			// evening: switch off
			off_evening();
	}

	return 0;
}

static void* xmas(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("XMAS Error setting pthread_setcancelstate");
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
			process(now, timing);
		}

		sleep(60);
	}
}

static int init() {
	if (pthread_create(&thread, NULL, &xmas, NULL))
		xlog("XMAS Error creating thread");

	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("XMAS Error canceling thread");

	if (pthread_join(thread, NULL))
		xlog("XMAS Error joining thread");
}

MCP_REGISTER(xmas, 6, &init, &stop);
