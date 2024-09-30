#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>

#include "shutter-config.h"
#include "utils.h"
#include "mqtt.h"
#include "mcp.h"

static void summer(struct tm *now, potd_t *potd) {
	float temp = sensors->sht31_temp;
	uint16_t lumi = sensors->bh1750_lux_mean;
	if (lumi == UINT16_MAX || temp == UINT16_MAX) {
		xlog("SHUTTER Error no sensor data");
		return;
	}

	int hot = temp >= potd->temp && lumi >= potd->lumi;
	// xdebug("SHUTTER %s program temp=%2.1f lumi=%d hot=%d", potd->name, temp, lumi, hot);

	for (shutter_t **potds = potd->shutters; *potds != NULL; potds++) {
		shutter_t *s = *potds;

		int down = s->down_from <= now->tm_hour && now->tm_hour < s->down_to;

		// down
		if (!s->lock_down && down && hot) {
			xlog("SHUTTER trigger %s DOWN %s at temp=%2.1f lumi=%d", potd->name, s->name, temp, lumi);
			tasmota_shutter(s->id, s->down);
			s->lock_down = 1;
			s->lock_up = 0;
			continue;
		}

		// up
		if (!s->lock_up && !down) {
			xlog("SHUTTER trigger %s UP %s at temp=%2.1f lumi=%d", potd->name, s->name, temp, lumi);
			tasmota_shutter(s->id, SHUTTER_UP);
			s->lock_up = 1;
			s->lock_down = 0;
			continue;
		}
	}
}

static void winter(struct tm *now, potd_t *potd) {
	float temp = sensors->sht31_temp;
	uint16_t lumi = sensors->bh1750_lux_mean;
	if (lumi == UINT16_MAX || temp == UINT16_MAX) {
		xlog("SHUTTER Error no sensor data");
		return;
	}

	int down = now->tm_hour > 12 && lumi <= potd->lumi && temp <= potd->temp;
	int up = !down && lumi >= potd->lumi;
	// xdebug("SHUTTER %s program temp=%2.1f lumi=%d", potd->name, temp, lumi);

	for (shutter_t **potds = potd->shutters; *potds != NULL; potds++) {
		shutter_t *s = *potds;

		// down
		if (!s->lock_down && down) {
			xlog("SHUTTER trigger %s DOWN %s at temp=%2.1f lumi=%d", potd->name, s->name, temp, lumi);
			tasmota_shutter(s->id, SHUTTER_DOWN);
			s->lock_down = 1;
			s->lock_up = 0;
			continue;
		}

		// up
		if (!s->lock_up && up) {
			xlog("SHUTTER trigger %s UP %s at temp=%2.1f lumi=%d", potd->name, s->name, temp, lumi);
			tasmota_shutter(s->id, SHUTTER_UP);
			s->lock_up = 1;
			s->lock_down = 0;
			continue;
		}
	}
}

static void loop() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("SHUTTER Error setting pthread_setcancelstate");
		return;
	}

	// wait for sensors
	sleep(3);

	while (1) {
		time_t now_ts = time(NULL);
		struct tm *ltstatic = localtime(&now_ts);

		if (SUMMER.months[ltstatic->tm_mon])
			summer(ltstatic, &SUMMER);
		else if (WINTER.months[ltstatic->tm_mon])
			winter(ltstatic, &WINTER);

		sleep(60);
	}
}

static int init() {
	return 0;
}

static void stop() {
}

MCP_REGISTER(shutter, 6, &init, &stop, &loop);
