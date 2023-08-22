#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>

#include "shutter.h"
#include "tasmota.h"
#include "utils.h"
#include "mqtt.h"
#include "mcp.h"

// these tasmota devices will be controlled in summer months
static const unsigned int summer_months[] = { SUMMER_MONTHS };
static const unsigned int summer_device[] = { SUMMER_DEVICES };

// these tasmota devices will be controlled in winter months
static const unsigned int winter_months[] = { WINTER_MONTHS };
static const unsigned int winter_device[] = { WINTER_DEVICES };

// do automatic shutter up/down movements only once per day
static int lock_morning = 0;
static int lock_afternoon = 0;

static pthread_t thread;

static void up_summer() {
	for (int i = 0; i < ARRAY_SIZE(summer_device); i++)
		tasmota_shutter(summer_device[i], SHUTTER_UP);
}

static void down_summer() {
	for (int i = 0; i < ARRAY_SIZE(summer_device); i++)
		tasmota_shutter(summer_device[i], SHUTTER_HALF);
}

static void up_winter() {
	for (int i = 0; i < ARRAY_SIZE(winter_device); i++)
		tasmota_shutter(winter_device[i], SHUTTER_UP);
}

static void down_winter() {
	for (int i = 0; i < ARRAY_SIZE(winter_device); i++)
		tasmota_shutter(winter_device[i], SHUTTER_DOWN);
}

static int summer(struct tm *now) {
	int lumi = sensors->bh1750_lux_mean;
	int temp = sensors->bmp280_temp;
	int morning = now->tm_hour < 12 ? 1 : 0;

	if (lumi == INT_MAX || temp == INT_MAX)
		return xerr("SHUTTER no sensor data");

	if (morning) {

		// release the afternoon lock
		lock_afternoon = 0;

		// no further actions
		if (lock_morning)
			return 0;

		// morning: check if 1. big light, 2. temp is above
		if (lumi > SUMMER_SUNRISE && temp > SUMMER_TEMP) {
			xlog("SHUTTER reached DOWN_SUMMER at lum %d temp %.1f", lumi, temp);
			down_summer();
			lock_morning = 1;
		}

	} else {

		// release the morning lock
		lock_morning = 0;

		// no further actions
		if (lock_afternoon)
			return 0;

		// evening: check if sundown is reached
		if (lumi < SUMMER_SUNDOWN) {
			xlog("SHUTTER reached UP_SUMMER at lum %d temp %.1f", lumi, temp);
			up_summer();
			lock_afternoon = 1;
		}

	}

	return 0;
}

static int winter(struct tm *now) {
	int lumi = sensors->bh1750_lux_mean;
	int temp = sensors->bmp280_temp;
	int afternoon = now->tm_hour < 12 ? 0 : 1;

	if (lumi == INT_MAX || temp == INT_MAX)
		return xerr("SHUTTER no sensor data");

	if (afternoon) {

		// release the morning lock
		lock_morning = 0;

		// no further actions
		if (lock_afternoon)
			return 0;

		// evening: check if 1. sundown is reached, 2. temp is below
		if (lumi < WINTER_SUNDOWN && temp < WINTER_TEMP) {
			xlog("SHUTTER reached DOWN_WINTER at lum %d temp %.1f", lumi, temp);
			down_winter();
			lock_afternoon = 1;
		}

	} else {

		// release the afternoon lock
		lock_afternoon = 0;

		// no further actions
		if (lock_morning)
			return 0;

		// morning: check if sunrise is reached
		if (lumi > WINTER_SUNRISE) {
			xlog("SHUTTER reached UP_WINTER at lum %d temp %.1f", lumi, temp);
			up_winter();
			lock_morning = 1;
		}

	}

	return 0;
}

static void* shutter(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("SHUTTER Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	// request shutter positions
	for (int i = 0; i < ARRAY_SIZE(winter_device); i++)
		tasmota_shutter(winter_device[i], SHUTTER_POS);
	for (int i = 0; i < ARRAY_SIZE(summer_device); i++)
		tasmota_shutter(summer_device[i], SHUTTER_POS);

	while (1) {
		time_t now_ts = time(NULL);
		struct tm *now = localtime(&now_ts);

		for (int i = 0; i < ARRAY_SIZE(summer_months); i++)
			if (now->tm_mon + 1 == summer_months[i])
				summer(now);

		for (int i = 0; i < ARRAY_SIZE(winter_months); i++)
			if (now->tm_mon + 1 == winter_months[i])
				winter(now);

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
