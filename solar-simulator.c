#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "solar-common.h"
#include "sensors.h"
#include "utils.h"
#include "mcp.h"

// run on tron12 - use sensors from picam
#define TEMP_IN					(tasmota_get_by_id(PICAM_SENSORS) ? tasmota_get_by_id(PICAM_SENSORS)->bmp085_temp : UINT16_MAX)
#define TEMP_OUT				(tasmota_get_by_id(PICAM_SENSORS) ? tasmota_get_by_id(PICAM_SENSORS)->bmp085_temp : UINT16_MAX)
#define LUMI					(tasmota_get_by_id(PICAM_SENSORS) ? tasmota_get_by_id(PICAM_SENSORS)->bh1750_lux : UINT16_MAX)

int akku_capacity() {
	return 0;
}

int akku_get_min_soc() {
	return 0;
}

void akku_set_min_soc(int min) {
}

void akku_state(device_t *akku) {
	// dummy implementation
	akku->state = Auto;
	akku->power = 0;
	akku->total = 0;
}

int akku_standby(device_t *akku) {
	// dummy implementation
	akku->state = Standby;
	akku->power = 0;
	akku->total = 0;
	return 0;
}

int akku_charge(device_t *akku, int limit) {
	// dummy implementation
	akku->state = Charge;
	akku->power = 0;
	akku->total = 0;
	return 0;
}

int akku_discharge(device_t *akku, int limit) {
	// dummy implementation
	akku->state = Discharge;
	akku->power = 0;
	akku->total = 0;
	return 0;
}

void inverter_status(int *inv1, int *inv2) {
	*inv1 = *inv2 = 0;
}

static void loop() {
	int load = 0, grid = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	while (1) {
		WAIT_NEXT_SECOND

		int mppt = sensors->lumi / 10;
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
