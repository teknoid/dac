#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <math.h>

#include "tasmota-devices.h"
#include "solar-common.h"
#include "frozen.h"
#include "curl.h"
#include "utils.h"

#define AKKU_BURNOUT			1
#define BASELOAD				(WINTER ? 300 : 200)
#define MINIMUM					(BASELOAD / 2)

#define TEMP_IN					sensors->htu21_temp
#define TEMP_OUT				sensors->sht31_temp
#define LUMI					sensors->bh1750_lux

// devices
static device_t a1 = { .name = "akku", .total = 0, .ramp = &ramp_akku, .adj = 0 }, *AKKU = &a1;
static device_t b1 = { .name = "boiler1", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t h1 = { .name = "heater1", .total = 1000, .ramp = &ramp_heater, .adj = 0, .id = 0, };
static device_t h2 = { .name = "heater2", .total = 500, .ramp = &ramp_heater, .adj = 0, .id = 0, };

// all devices, needed for initialization
static device_t *DEVICES[] = { &h1, &h2, &b1, &a1, 0 };

// define POTDs
static const potd_t MODEST = { .name = "MODEST", .devices = DEVICES };
static const potd_t GREEDY = { .name = "GREEDY", .devices = DEVICES };
static const potd_t PLENTY = { .name = "PLENTY", .devices = DEVICES };
static const potd_t BOILERS = { .name = "BOILERS", .devices = DEVICES };
static const potd_t BOILER1 = { .name = "BOILER1", .devices = DEVICES };
static const potd_t BOILER3 = { .name = "BOILER3", .devices = DEVICES };

#define MIN_SOC					50
#define AKKU_CHARGE_MAX			4500
#define AKKU_DISCHARGE_MAX		4500
#define AKKU_CAPACITY			11000

static pthread_t thread_update;

static int mpptx(int offset, int peak) {
	// minutes around 12 high noon
	int minute = (12 + offset) * 60 - now->tm_hour * 60;
	if (minute < -180)
		minute = -180;
	if (minute > 180)
		minute = 180;

	double rad = abs(minute) * M_PI / 180.0;
	double sinus = sin(rad);

	double lumi = LUMI ? (1 - 1 / LUMI) : 0;
	int pv = sinus * peak * lumi;
	xlog("minute=%d sin=%.2f pv=%d LUMI=%d lumi=%.1f", minute, sinus, pv, LUMI, lumi);
	return pv;
}

static int mppt(int offset, int peak) {
	return LUMI / 10;
}

static void* update(void *arg) {
	int load = 0, grid = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {

		// wait for next second
		time_t now_ts = time(NULL);
		while (now_ts == time(NULL))
			msleep(100);

		int mppt1 = mppt(-3, 6000);
		int mppt2 = mppt(0, 3000);
		int mppt3 = mppt(3, 3000) / 2;
		int mppt4 = mppt(6, 3000) / 2;

		// simulate load change between 100 and 200 watts every 100 seconds
		if (load == 0 || now_ts % 100 == 0) {
			load = 100 + rand() % 100 + 1;
			grid = (mppt1 + mppt2 + mppt3 + mppt4 - load) * -1;
		}

		pthread_mutex_lock(&pstate_lock);

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

		pthread_mutex_unlock(&pstate_lock);
	}
}

static int solar_init() {
	// start updater thread
	if (pthread_create(&thread_update, NULL, &update, NULL))
		return xerr("Error creating thread_update");

	// initialize random number generator
	srand(time(NULL));

	return 0;
}

static void solar_stop() {
	if (pthread_cancel(thread_update))
		xlog("Error canceling thread_update");

	if (pthread_join(thread_update, NULL))
		xlog("Error joining thread_update");
}

static void inverter_status(char *line) {
	// unimplemented
}

static void inverter_valid() {
	// unimplemented
}

static void akku_strategy() {
	// unimplemented
}

static int akku_standby() {
	// dummy implementation
	AKKU->state = Standby;
	AKKU->power = 0;
	return 0; // continue loop
}

static int akku_charge() {
	// dummy implementation
	AKKU->state = Charge;
	AKKU->power = 1;
	return 0; // continue loop
}

static int akku_discharge() {
	// dummy implementation
	AKKU->state = Discharge;
	AKKU->power = 0;
	return 0; // continue loop
}

static int battery(char *arg) {
	return 0; // unimplemented
}

static int storage_min(char *arg) {
	return 0; // unimplemented
}

static int grid() {
	return 0; // unimplemented
}

static int calibrate(char *name) {
	return 0; // unimplemented
}
