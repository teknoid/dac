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

// program of the day - summer or winter
static potd_t *potd;

//
// summer mode
//

static int summer(struct tm *now, unsigned int lumi, int temp) {
	xdebug("SHUTTER %s program lumi %d temp %d", potd->name, lumi, temp);

	int hot = lumi > potd->lumi && temp > potd->temp;
	for (shutter_t **potds = potd->shutters; *potds != NULL; potds++) {
		shutter_t *s = *potds;

		// down
		if (!s->lock_down && s->down_from <= now->tm_hour && now->tm_hour < s->down_to && hot) {
			xlog("SHUTTER trigger %s DOWN %s at lumi %d temp %d", potd->name, s->name, lumi, temp);
			tasmota_shutter(s->id, s->down);
			s->lock_down = 1;
			s->lock_up = 0;
			continue;
		}

		// up
		if (!s->lock_up && s->down_to <= now->tm_hour && now->tm_hour < s->down_from) {
			xlog("SHUTTER trigger %s UP %s at lumi %d temp %d", potd->name, s->name, lumi, temp);
			tasmota_shutter(s->id, SHUTTER_UP);
			s->lock_up = 1;
			s->lock_down = 0;
			continue;
		}

	}
	return 0;
}

//
// winter mode
//

static int winter(struct tm *now, unsigned int lumi, int temp) {
	xdebug("SHUTTER %s program lumi %d temp %d", potd->name, lumi, temp);

	int down = now->tm_hour > 12 && lumi < potd->lumi && temp < potd->temp;
	int up = now->tm_hour < 12 && lumi > potd->lumi;

	for (shutter_t **potds = potd->shutters; *potds != NULL; potds++) {
		shutter_t *s = *potds;

		// down
		if (!s->lock_down && down) {
			xlog("SHUTTER trigger %s DOWN %s at lumi %d temp %d", potd->name, s->name, lumi, temp);
			tasmota_shutter(s->id, SHUTTER_DOWN);
			s->lock_down = 1;
			s->lock_up = 0;
			continue;
		}

		// up
		if (!s->lock_up && up) {
			xlog("SHUTTER trigger %s UP %s at lumi %d temp %d", potd->name, s->name, lumi, temp);
			tasmota_shutter(s->id, SHUTTER_UP);
			s->lock_up = 1;
			s->lock_down = 0;
			continue;
		}
	}

	return 0;
}

static void shutter() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("SHUTTER Error setting pthread_setcancelstate");
		return;
	}

	sleep(3); // wait for sensors
	unsigned int lumi = sensors->bh1750_lux_mean;
	int temp = (int) sensors->bmp280_temp;
	if (lumi == UINT16_MAX || temp == UINT16_MAX) {
		xlog("SHUTTER Error no sensor data");
		return;
	}

	while (1) {
		time_t now_ts = time(NULL);
		struct tm *now = localtime(&now_ts);

		lumi = sensors->bh1750_lux_mean;
		temp = (int) sensors->bmp280_temp;

		if (SUMMER.months[now->tm_mon + 1]) {
			potd = &SUMMER;
			summer(now, lumi, temp);
		} else if (WINTER.months[now->tm_mon + 1]) {
			potd = &WINTER;
			winter(now, lumi, temp);
		} else
			potd = NULL;

		sleep(60);
	}
}

static int init() {
	return 0;
}

static void stop() {
}

MCP_REGISTER(shutter, 6, &init, &stop, &shutter);
