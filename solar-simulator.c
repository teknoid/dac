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

// thread synchronization
static sem_t start, finish;

void read_trigger() {
	sem_post(&start);
}

void read_wait() {
	sem_wait(&finish);
}

// unimplemented
int inverter_on() {
	return 0;
}

// unimplemented
int inverter_off() {
	return 0;
}

// dummy implementation
int akku_auto(device_t *akku) {
	akku->state = Auto;
	akku->power = 0;
	akku->total = 0;
	return 0;
}

// dummy implementation
int akku_standby(device_t *akku) {
	akku->state = Standby;
	akku->power = 0;
	akku->total = 0;
	return 0;
}

// dummy implementation
int akku_charge(device_t *akku) {
	akku->state = Charge;
	akku->power = 0;
	akku->total = 0;
	return 0;
}

// dummy implementation
int akku_discharge(device_t *akku) {
	akku->state = Discharge;
	akku->power = 0;
	akku->total = 0;
	return 0;
}

// dummy implementation
void akku_state(device_t *akku) {
	akku->state = Auto;
	akku->power = 0;
	akku->total = 0;
}

static void loop() {
	int load = 0, grid = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	while (1) {
		sem_wait(&start);

		int mppt = sensor->lumi / 10;
		int mppt1 = mppt;
		int mppt2 = mppt;
		int mppt3 = mppt / 2;
		int mppt4 = mppt / 2;

		// simulate load change between 100 and 200 watts every 100 seconds
		if (load == 0 || time(NULL) % 100 == 0)
			load = (100 + rand() % 100 + 1) * -1;
		grid = (mppt1 + mppt2 + mppt3 + mppt4 + load) * -1;

		gstate->soc = 500;

		pstate->ac1 = mppt1 + mppt2;
		pstate->dc1 = mppt1 + mppt2;
		pstate->mppt1p = mppt1;
		pstate->mppt2p = mppt2;

		pstate->ac2 = mppt3 + mppt4;
		pstate->dc2 = mppt3 + mppt4;
		pstate->mppt3p = mppt3;
		pstate->mppt4p = mppt4;

		pstate->grid = grid;
		pstate->akku = 0;
		pstate->l1p = 0;
		pstate->l2p = 0;
		pstate->l3p = 0;
		pstate->l1v = 0;
		pstate->l2v = 0;
		pstate->l3v = 0;
		pstate->f = 0;

		sem_post(&finish);
	}
}

static int init() {
	// initialize random number generator
	srand(time(NULL));

	sem_init(&start, 0, 0);
	sem_init(&finish, 0, 0);

	return 0;
}

static void stop() {
	sem_close(&start);
	sem_close(&finish);
}

MCP_REGISTER(solar_simulator, 10, &init, &stop, &loop);
