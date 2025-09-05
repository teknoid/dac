#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>

#include "ledstrip.h"
#include "flamingo.h"
#include "tasmota.h"
#include "sensors.h"
#include "utils.h"
#include "xmas.h"
#include "mcp.h"

#ifndef LUMI
#define LUMI					6666
#endif

// light on: ↑earlier, ↓later
#ifndef SUNDOWN
#define SUNDOWN					50
#endif

// light off: ↑later ↓earlier
#ifndef SUNRISE
#define SUNRISE					100
#endif

// TODO define channel status for each remote control unit
static char channel_status[128];

static int power = -1;

void xmas_on() {
#ifndef PICAM
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		const xmas_t *device = &devices[i];
		tasmota_power(device->id, device->relay, 1);
	}
	ledstrip_mode_fade();
#endif
}

void xmas_off() {
#ifndef PICAM
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		const xmas_t *device = &devices[i];
		tasmota_power(device->id, device->relay, 0);
	}
	ledstrip_off();
#endif
}

static void xmas_on_flamingo(const xmas_timing_t *timing) {
	int index = timing->channel - 'A';
	if (!channel_status[index]) {
		xlog("flamingo_send_FA500 %d %c 1", timing->remote, timing->channel);
		flamingo_send_FA500(timing->remote, timing->channel, 1, -1);
		channel_status[index] = 1;
	}
}

static void xmas_off_flamingo(const xmas_timing_t *timing) {
	int index = timing->channel - 'A';
	if (channel_status[index]) {
		xlog("flamingo_send_FA500 %d %c 0", timing->remote, timing->channel);
		flamingo_send_FA500(timing->remote, timing->channel, 0, -1);
		channel_status[index] = 0;
	}
}

static void on_sundown(const xmas_timing_t *timing) {
	xlog("XMAS reached SUNDOWN at %d", LUMI);
	xmas_on();
	xmas_on_flamingo(timing);
	power = 1;
}

static void on_morning(const xmas_timing_t *timing) {
	xlog("XMAS reached ON time frame");
	xmas_on();
	xmas_on_flamingo(timing);
	power = 1;
}

static void off_sunrise(const xmas_timing_t *timing) {
	xlog("XMAS reached SUNRISE at %d", LUMI);
	xmas_off();
	xmas_off_flamingo(timing);
	power = 0;
}

static void off_evening(const xmas_timing_t *timing) {
	xlog("XMAS reached OFF time frame");
	xmas_off();
	xmas_off_flamingo(timing);
	power = 0;
}

static int process(int h, int m, const xmas_timing_t *timing) {
	int afternoon = h < 12 ? 0 : 1;
	int curr = h * 60 + m;
	int from = timing->on_h * 60 + timing->on_m;
	int to = timing->off_h * 60 + timing->off_m;

	if (from <= curr && curr <= to) {
		// ON time frame

		// already on
		if (power == 1)
			return 0;

		if (afternoon) {
			// evening: check if sundown is reached an switch on
			if (LUMI < SUNDOWN)
				on_sundown(timing);
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
			if (LUMI > SUNRISE)
				off_sunrise(timing);
			else
				// xlog("in OFF time, waiting for XMAS_SUNRISE(%d:%d)", lumi, XMAS_SUNRISE);
				return 0;
		else
			// evening: switch off
			off_evening(timing);
	}

	return 0;
}

static void xmas() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("XMAS Error setting pthread_setcancelstate");
		return;
	}

	// elevate realtime priority for flamingo 433MHz transmit
	if (elevate_realtime(1) < 0) {
		xlog("XMAS Error elevating realtime");
		return;
	}

	sleep(3); // wait for sensors
	if (LUMI == UINT16_MAX) {
		xlog("XMAS Error no sensor data");
		return;
	}

	while (1) {
		LOCALTIME

		for (int i = 0; i < ARRAY_SIZE(timings); i++) {
			const xmas_timing_t *timing = &timings[i];

			if (!timing->active)
				continue;

			if (now->tm_wday != timing->wday)
				continue;

			// xlog("processing timing[%i]", i);
			process(now->tm_hour, now->tm_min, timing);
		}

		sleep(60);
	}
}

static int init() {
	return 0;
}

static void stop() {
}

MCP_REGISTER(xmas, 6, &init, &stop, &xmas);
