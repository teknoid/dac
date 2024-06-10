#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>

#include "shutter-config.h"
#include "utils.h"
#include "mqtt.h"
#include "mcp.h"

// program of the day - summer or winter
static potd_t *potd;
static int reset_needed;

static pthread_t thread;

//
// summer mode
//

static int summer(struct tm *now, unsigned int lumi, int temp) {
	xdebug("SHUTTER %s program lumi %d temp %d", potd->name, lumi, temp);

	int hot = lumi > potd->lumi && temp > potd->temp;
	for (shutter_t **ss = potd->shutters; *ss != NULL; ss++) {
		shutter_t *s = *ss;

		// down
		if (!s->lock_down && s->down_from <= now->tm_hour && now->tm_hour < s->down_to && hot) {
			xlog("SHUTTER trigger %s DOWN at lumi %d temp %d", potd->name, lumi, temp);
			tasmota_shutter(s->id, s->down);
			s->lock_down = 1;
			reset_needed = 1;
		}

		// up
		if (!s->lock_up && s->down_to <= now->tm_hour && now->tm_hour < s->down_from) {
			xlog("SHUTTER trigger %s UP at lumi %d temp %d", potd->name, lumi, temp);
			tasmota_shutter(s->id, SHUTTER_UP);
			s->lock_up = 1;
			reset_needed = 1;
		}

	}
	return 0;
}

//
// winter mode
//

static int winter(struct tm *now, unsigned int lumi, int temp) {
	xdebug("SHUTTER %s program lumi %d temp %d", potd->name, lumi, temp);

	int afternoon = now->tm_hour < 12 ? 0 : 1;
	if (afternoon) {

		// evening: check if 1. sundown is reached, 2. temp is below
		if (lumi < potd->lumi && temp < potd->temp) {
			xlog("SHUTTER trigger %s DOWN at lumi %d temp %d", potd->name, lumi, temp);
			for (shutter_t **ss = potd->shutters; *ss != NULL; ss++) {
				shutter_t *s = *ss;
				if (!s->lock_down) {
					tasmota_shutter(s->id, SHUTTER_DOWN);
					s->lock_down = 1;
					reset_needed = 1;
				}
			}
		}

	} else {

		// morning: check if sunrise is reached
		if (lumi > potd->lumi) {
			xlog("SHUTTER trigger %s UP at lumi %d temp %d", potd->name, lumi, temp);
			for (shutter_t **ss = potd->shutters; *ss != NULL; ss++) {
				shutter_t *s = *ss;
				if (!s->lock_up) {
					tasmota_shutter(s->id, SHUTTER_UP);
					s->lock_up = 1;
					reset_needed = 1;
				}
			}
		}
	}

	return 0;
}

static int reset() {
	if (potd == NULL)
		return 0;

	for (shutter_t **ss = potd->shutters; *ss != NULL; ss++) {
		shutter_t *s = *ss;
		s->lock_down = 0;
		s->lock_up = 0;
	}

	return 0;
}

static void* shutter(void *arg) {
	unsigned int lumi;
	int temp;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("SHUTTER Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	sleep(1); // wait for sensors
	lumi = sensors->bh1750_lux_mean;
	temp = (int) sensors->bmp280_temp;
	if (lumi == UINT16_MAX || temp == UINT16_MAX) {
		xlog("SHUTTER no sensor data");
		return (void*) 0;
	}

	while (1) {
		time_t now_ts = time(NULL);
		struct tm *now = localtime(&now_ts);

		lumi = sensors->bh1750_lux_mean;
		temp = (int) sensors->bmp280_temp;

		// reset locks once per day
		if (now->tm_hour == 0 && reset_needed)
			reset_needed = reset();

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
	if (pthread_create(&thread, NULL, &shutter, NULL))
		xlog("SHUTTER Error creating thread");

	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("SHUTTER Error canceling thread");

	if (pthread_join(thread, NULL))
		xlog("SHUTTER Error joining thread");
}

MCP_REGISTER(shutter, 6, &init, &stop);
