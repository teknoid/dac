#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#include "utils.h"
#include "xmas.h"
#include "mqtt.h"
#include "mcp.h"

static pthread_t thread;
static int power = -1;

static void on(const timing_t *timing) {
	char subtopic[64];
	snprintf(subtopic, sizeof(subtopic), "shelly/%s/cmnd/POWER", PLUG1);
	publish(subtopic, ON);
	power = 1;
	xlog("XMAS switched ON");
}

static void off(const timing_t *timing) {
	char subtopic[64];
	snprintf(subtopic, sizeof(subtopic), "shelly/%s/cmnd/POWER", PLUG1);
	publish(subtopic, OFF);
	power = 0;
	xlog("XMAS switched OFF");
}

static int process(struct tm *now, const timing_t *timing) {
	int lumi = sensors->bh1750_lux;
	int afternoon = now->tm_hour < 12 ? 0 : 1;
	int curr = now->tm_hour * 60 + now->tm_min;
	int from = timing->on_h * 60 + timing->on_m;
	int to = timing->off_h * 60 + timing->off_m;

	if (lumi < 0)
		return xerr("XMAS no sensor data");

	if (from <= curr && curr <= to) {
		// ON time frame

		// already on
		if (power == 1)
			return 0;

		if (afternoon) {
			// evening: check if sundown is reached an switch on
			if (lumi < XMAS_SUNDOWN)
				on(timing);
			else
				// logger.info("in ON time, waiting for XMAS_SUNDOWN: " + lumi);
				return 0;
		} else
			// morning: switch on
			on(timing);

	} else {
		// OFF time frame

		// already off
		if (power == 0)
			return 0;

		if (!afternoon)
			// morning: check if sunrise is reached an switch off
			if (lumi > XMAS_SUNRISE)
				off(timing);
			else
				// logger.info("in OFF time, waiting for XMAS_SUNRISE: " + lumi);
				return 0;
		else
			// evening: switch off
			off(timing);
	}

	return 0;
}

static void* xmas(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
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
		xlog("Error creating thread");

	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("Error canceling thread");

	if (pthread_join(thread, NULL))
		xlog("Error joining thread");
}

MCP_REGISTER(xmas, 6, &init, &stop);
