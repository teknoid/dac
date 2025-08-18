#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include "tasmota-devices.h"
#include "tasmota.h"
#include "sensors.h"
#include "utils.h"
#include "aqua.h"
#include "mcp.h"

static void set_pump(int value) {
	xdebug("AQUA set_pump %d", value);

#ifndef AQUA_MAIN
	// tasmota_power(CARPORT, 1, value);
#endif
}

static void set_valve(int valve, int value) {
	xdebug("AQUA set_valve %d %d", valve, value);
}

static int get_rain(int valve, int hour) {
	int rain = 0;

	for (int i = 0; i < ARRAY_SIZE(POTD); i++) {
		const aqua_t *a = &POTD[i];

		int h = a->hr[hour];
		int v = a->v[valve];

		if (!h || !v)
			continue; // valve not active for this hour

		int temp_ok = TEMP >= a->t;
		int humi_ok = HUMI <= a->h;
		int lumi_ok = LUMI >= a->l;

		if (a->l == 0 && a->t == 0) {
			// check only humidity
			xdebug("AQUA check potd %s for valve=%d rain=%d humi=%d", a->n, valve, a->r, humi_ok);
			if (humi_ok)
				rain = a->r > rain ? a->r : rain;

		} else if (a->l == 0) {
			// check humidity and temperature
			xdebug("AQUA check potd %s for valve=%d rain=%d humi=%d temp=%d", a->n, valve, a->r, humi_ok, temp_ok);
			if (humi_ok && temp_ok)
				rain = a->r > rain ? a->r : rain;

		} else if (a->t == 0) {
			// check humidity and luminousity
			xdebug("AQUA check potd %s for valve=%d rain=%d humi=%d lumi=%d", a->n, valve, a->r, humi_ok, lumi_ok);
			if (humi_ok && lumi_ok)
				rain = a->r > rain ? a->r : rain;

		} else {
			// check all 3: humidity, temperature and luminousity
			xdebug("AQUA check potd %s for valve=%d rain=%d humi=%d lumi=%d temp=%d", a->n, valve, a->r, humi_ok, lumi_ok, temp_ok);
			if (humi_ok && lumi_ok && temp_ok)
				rain = a->r > rain ? a->r : rain;
		}
	}

	return rain;
}

static void process(int hour) {
	xdebug("AQUA sensors temp=%.1f humi=%.1f lumi=%d", TEMP, HUMI, LUMI);
	if (TEMP > 1000 || HUMI > 1000 || LUMI == UINT16_MAX) {
		xlog("AQUA Error no sensor data");
		return;
	}

	for (int v = 0; v < VALVES; v++) {
		int rain = get_rain(v, hour);
		if (rain) {
			xlog("AQUA raining at valve %d for %d seconds", v, rain);
			set_valve(v, 1);
			set_pump(1);
			sleep(rain);
			set_valve(v, 0);
		}
	}

	set_pump(0);
}

static void loop() {
	int hour = -1;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// the AQUA main loop
	while (1) {
		LOCALTIME

		// first run on next full hour
		if (hour == -1)
			hour = now->tm_hour;

		if (hour != now->tm_hour) {
			hour = now->tm_hour;
			process(hour);
		}

		sleep(60);
	}
}

static int init() {

	// stop pump and close all valves
	set_pump(0);
	for (int v = 0; v < VALVES; v++)
		set_valve(v, 0);

	return 0;
}

static void stop() {
}

static int test() {
	xlog("module test code");
	return 0;
}

int aqua_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	// no arguments - main loop
	if (argc == 1) {
		init();
		loop();
		pause();
		stop();
		return 0;
	}

	// with arguments
	int c;
	while ((c = getopt(argc, argv, "t")) != -1) {
		switch (c) {
		case 't':
			return test();
		default:
			xlog("unknown getopt %c", c);
		}
	}

	return 0;
}

#ifdef AQUA_MAIN
int main(int argc, char **argv) {
	return aqua_main(argc, argv);
}
#else
MCP_REGISTER(aqua, 7, &init, &stop, &loop);
#endif
