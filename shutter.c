#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>

#include "tasmota.h"
#include "shutter-config.h"
#include "sensors.h"
#include "utils.h"
#include "mqtt.h"
#include "mcp.h"

static void summer(struct tm *now, potd_t *potd) {
	if (sensors->tout >= UINT16_MAX || sensors->lumi == UINT16_MAX) {
		xlog("SHUTTER Warning no sensor data");
		return;
	}

	int hot = sensors->tout >= potd->temp && sensors->lumi >= potd->lumi;
	// xdebug("SHUTTER %s program temp=%.1f lumi=%d hot=%d", potd->name, temp, lumi, hot);

	for (shutter_t **potds = potd->shutters; *potds != NULL; potds++) {
		shutter_t *s = *potds;

		int down = s->down_from <= now->tm_hour && now->tm_hour < s->down_to;

		// down
		if (!s->lock_down && down && hot) {
			xlog("SHUTTER trigger %s DOWN %s at temp=%.1f lumi=%d", potd->name, s->name, sensors->tout, sensors->lumi);
			tasmota_shutter(s->id, s->down);
			s->lock_down = 1;
			s->lock_up = 0;
			continue;
		}

		// up
		if (!s->lock_up && !down) {
			xlog("SHUTTER trigger %s UP %s at temp=%.1f lumi=%d", potd->name, s->name, sensors->tout, sensors->lumi);
			tasmota_shutter(s->id, SHUTTER_UP);
			s->lock_up = 1;
			s->lock_down = 0;
			continue;
		}
	}
}

static void winter(struct tm *now, potd_t *potd) {
	if (sensors->tout >= UINT16_MAX || sensors->lumi == UINT16_MAX) {
		xlog("SHUTTER Warning no sensor data");
		return;
	}

	int down = now->tm_hour > 12 && sensors->lumi <= potd->lumi && sensors->tout <= potd->temp;
	int up = !down && sensors->lumi >= potd->lumi;
	// xdebug("SHUTTER %s program temp=%.1f lumi=%d", potd->name, temp, lumi);

	for (shutter_t **potds = potd->shutters; *potds != NULL; potds++) {
		shutter_t *s = *potds;

		// down
		if (!s->lock_down && down) {
			xlog("SHUTTER trigger %s DOWN %s at temp=%.1f lumi=%d", potd->name, s->name, sensors->tout, sensors->lumi);
			tasmota_shutter(s->id, SHUTTER_DOWN);
			s->lock_down = 1;
			s->lock_up = 0;
			continue;
		}

		// up
		if (!s->lock_up && up) {
			xlog("SHUTTER trigger %s UP %s at temp=%.1f lumi=%d", potd->name, s->name, sensors->tout, sensors->lumi);
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

	while (1) {
		LOCALTIME

		if (SUMMER.months[now->tm_mon])
			summer(now, &SUMMER);
		else if (WINTER.months[now->tm_mon])
			winter(now, &WINTER);

		sleep(60);
	}
}

static int init() {
	return 0;
}

static void stop() {
}

MCP_REGISTER(shutter, 6, &init, &stop, &loop);
