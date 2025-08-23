// loop
// gcc -DMCP -I./include -o solar mcp.c solar-simulator.c solar-collector.c solar-dispatcher.c utils.c mosmix.c sensors.c i2c.c -lm

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "solar-common.h"
#include "utils.h"
#include "mcp.h"

#ifdef MCP
#include "sensors.h"
#define TEMP_IN					sensors->bmp085_temp
#define TEMP_OUT				sensors->bmp085_temp
#define LUMI					sensors->bh1750_lux
#endif

#ifndef TEMP_IN
#define TEMP_IN					22.0
#endif

#ifndef TEMP_OUT
#define TEMP_OUT				15.0
#endif

#ifndef LUMI
#define LUMI					6666
#endif

int temp_in() {
	return TEMP_IN;
}

int temp_out() {
	return TEMP_OUT;
}

int akku_capacity() {
	return 0;
}

int akku_min_soc() {
	return 0;
}

int akku_charge_max() {
	return 0;
}

int akku_discharge_max() {
	return 0;
}

int akku_standby(device_t *akku) {
	// dummy implementation
	akku->state = Standby;
	akku->power = 0;
	return 0;
}

int akku_charge(device_t *akku) {
	// dummy implementation
	akku->state = Charge;
	akku->power = 1;
	return 0;
}

int akku_discharge(device_t *akku) {
	// dummy implementation
	akku->state = Discharge;
	akku->power = 0;
	return 0;
}

void inverter_status(char *line) {
	// unimplemented
}

void inverter_pstate_valid() {
	// unimplemented
}

static void loop() {
	int load = 0, grid = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	while (1) {
		WAIT_NEXT_SECOND

		int mppt = LUMI / 10;
		int mppt1 = mppt;
		int mppt2 = mppt;
		int mppt3 = mppt / 2;
		int mppt4 = mppt / 2;

		// simulate load change between 100 and 200 watts every 100 seconds
		if (load == 0 || ts_now % 100 == 0)
			load = (100 + rand() % 100 + 1) * -1;
		grid = (mppt1 + mppt2 + mppt3 + mppt4 + load) * -1;

		pthread_mutex_lock(&collector_lock);

		pstate->ac1 = mppt1 + mppt2;
		pstate->dc1 = mppt1 + mppt2;
		pstate->mppt1 = mppt1;
		pstate->mppt2 = mppt2;

		pstate->ac2 = mppt3 + mppt4;
		pstate->dc2 = mppt3 + mppt4;
		pstate->mppt3 = mppt3;
		pstate->mppt4 = mppt4;

		pstate->grid = grid;
		pstate->akku = 0;
		pstate->soc = 500;
		pstate->p1 = 0;
		pstate->p2 = 0;
		pstate->p3 = 0;
		pstate->v1 = 0;
		pstate->v2 = 0;
		pstate->v3 = 0;
		pstate->f = 0;

		pthread_mutex_unlock(&collector_lock);
	}
}

static int init() {
	// initialize random number generator
	srand(time(NULL));
	return 0;
}

static void stop() {
}

MCP_REGISTER(solar, 10, &init, &stop, &loop);
