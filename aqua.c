#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include "aqua-config.h"
#include "tasmota.h"
#include "utils.h"
#include "mcp.h"

static void set_pump(int value) {
	xdebug("AQUA set_pump %d", value);

#ifndef AQUA_MAIN
	tasmota_power(CARPORT, 3, value);
#endif
}

static void set_valve(int valve, int value) {
	xdebug("AQUA set_valve %d %d", valve, value);
}

static int valve(int valve, int hour, float temp, float humi, uint16_t lumi) {
	int rain = 0;

	for (int i = 0; i < ARRAY_SIZE(POTD); i++) {
		const aqua_t *a = &POTD[i];

		int h = a->hr[hour];
		int v = a->v[valve];

		if (!h || !v)
			continue; // valve not active for this hour

		int temp_ok = temp >= a->t;
		int humi_ok = humi <= a->h;
		int lumi_ok = lumi >= a->l;

		if (a->l == 0 && a->t == 0) {
			// check only humidity
			xdebug("AQUA check potd %s for valve %d humi=%d", a->n, valve, humi_ok);
			if (humi_ok)
				rain = a->r > rain ? a->r : rain;

		} else if (a->l == 0) {
			// check humidity and temperature
			xdebug("AQUA check potd %s for valve %d humi=%d temp=%d", a->n, valve, humi_ok, temp_ok);
			if (humi_ok && temp_ok)
				rain = a->r > rain ? a->r : rain;

		} else if (a->t == 0) {
			// check humidity and luminousity
			xdebug("AQUA check potd %s for valve %d humi=%d lumi=%d", a->n, valve, humi_ok, lumi_ok);
			if (humi_ok && lumi_ok)
				rain = a->r > rain ? a->r : rain;

		} else {
			// check all 3: humidity, temperature and luminousity
			xdebug("AQUA check potd %s for valve %d humi=%d lumi=%d temp=%d", a->n, valve, humi_ok, lumi_ok, temp_ok);
			if (humi_ok && lumi_ok && temp_ok)
				rain = a->r > rain ? a->r : rain;
		}
	}

	return rain;
}

static void process(int hour) {

#ifdef AQUA_MAIN
	float temp = 23.2;
	float humi = 33;
	uint16_t lumi = 35000;
#else
	float temp = sensors->bmp280_temp;
	float humi = sensors->sht31_humi;
	uint16_t lumi = sensors->bh1750_lux;
#endif

	xdebug("AQUA sensors temp=%2.1f humi=%3.1f lumi=%d", temp, humi, lumi);

	if (temp == UINT16_MAX || lumi == UINT16_MAX || humi == UINT16_MAX) {
		xlog("XMAS Error no sensor data");
		return;
	}

	for (int v = 0; v < 4; v++) {
		int rain = valve(v, hour, temp, humi, lumi);
		if (rain) {
			set_valve(v, 1);
			set_pump(1);
			xlog("AQUA raining valve %d for %d seconds", v, rain);
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

	// stop pump and close all valves
	set_pump(0);
	for (int v = 0; v < 4; v++)
		set_valve(v, 0);

	// wait for sensors
	sleep(3);

	// the AQUA main loop
	while (1) {

		time_t now_ts = time(NULL);
		struct tm *now = localtime(&now_ts);

		if (hour != now->tm_hour) {
			hour = now->tm_hour;
			process(hour);
		}

		sleep(60);
	}
}

static int init() {
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
