#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <signal.h>
#include <pthread.h>

#include <modbus/modbus.h>

#include "fronius-modbus.h"
#include "fronius-config.h"
#include "utils.h"

// global state with power flow data and calculations
static pstate_t pstate_history[PSTATE_HISTORY];
static pstate_t *pstate = pstate_history;
static int pstate_history_ptr = 0;

// Fronius modbus register banks
static sunspec_inverter_t *inverter10;
static sunspec_inverter_t *inverter7;
static sunspec_storage_t *storage;
static sunspec_meter_t *meter;

// Fronius modbus reader threads
static pthread_t thread_fronius10, thread_fronius7;

static struct tm now_tm, *now = &now_tm;

int set_heater(device_t *heater, int power) {
	return 0;
}

int set_boiler(device_t *boiler, int power) {
	return 0;
}

// get a power state history
static pstate_t* get_pstate_history(int offset) {
	int i = pstate_history_ptr + offset;
	if (i < 0)
		i += PSTATE_HISTORY;
	if (i >= PSTATE_HISTORY)
		i -= PSTATE_HISTORY;
	return &pstate_history[i];
}

// dump the power state history up to given rows
static void dump_pstate_history(int back) {
	char line[sizeof(pstate_t) * 8 + 16];
	char value[8];

	strcpy(line, "FRONIUS state  idx    pv   Δpv   grid  akku  surp waste   sum   soc  load Δload xload dxlod cload  pv10   pv7  dist  tend stdby  wait");
	xdebug(line);
	for (int y = 0; y < back; y++) {
		strcpy(line, "FRONIUS state ");
		snprintf(value, 8, "[%2d] ", y * -1);
		strcat(line, value);
		int *vv = (int*) get_pstate_history(y * -1);
		for (int x = 0; x < sizeof(pstate_t) / sizeof(int); x++) {
			snprintf(value, 8, x == 2 ? "%6d " : "%5d ", vv[x]);
			strcat(line, value);
		}
		xdebug(line);
	}
}

// initialize all devices with start values
static void init_all_devices() {
	for (device_t **d = DEVICES; *d != 0; d++) {
		(*d)->state = Active;
		(*d)->power = -1;
		(*d)->dload = 0;
		(*d)->addr = resolve_ip((*d)->name);
	}
}

//static void dump(uint16_t registers[], size_t size) {
//	for (int i = 0; i < size; i++)
//		xlog("reg[%d]=%05d (0x%04X)", i, registers[i], registers[i]);
//}

static void set_all_devices(int power) {
}

// TODO performance: make makro
//static int delta(int v, int v_old, int d_proz) {
//	// no tolerance
//	if (!d_proz)
//		return v != v_old;
//
//	// tolerance in d_proz (percent)
//	int v_diff = v - v_old;
//	int v_proz = v == 0 ? 0 : (v_diff * 100) / v;
//	// xlog("v_old=%d v=%d v_diff=%d v_proz=%d d_proz=%d", v_old, v, v_diff, v_proz, d_proz);
//	if (abs(v_proz) >= d_proz)
//		return 1;
//	return 0;
//}

static void daily() {
	xlog("executing daily tasks...");
}

static void hourly() {
	xlog("executing hourly tasks...");
}

static device_t* regulate() {
//	xlog("PhVphA %d (%2.1f)", SFI(inverter7->PhVphA, inverter7->V_SF), SFF(inverter7->PhVphA, inverter7->V_SF));
//	xlog("PhVphB %d (%2.1f)", SFI(inverter7->PhVphB, inverter7->V_SF), SFF(inverter7->PhVphB, inverter7->V_SF));
//	xlog("PhVphC %d (%2.1f)", SFI(inverter7->PhVphC, inverter7->V_SF), SFF(inverter7->PhVphC, inverter7->V_SF));
//
//	xlog("DCW    %d (%2.1f)", SFI(inverter10->DCW, inverter10->DCW_SF), SFF(inverter10->DCW, inverter10->DCW_SF));
//	xlog("W      %d (%2.1f)", SFI(inverter10->W, inverter10->W_SF), SFF(inverter10->W, inverter10->W_SF));
//
//	xlog("PV10=%d PV7=%d", state->pv10, state->pv7);

// takeover ramp()

	return 0;
}

static device_t* check_response(device_t *d) {
	return 0; // clear device
}

static void calculate() {
}

static int update() {
	// clear slot for current values
	pstate = &pstate_history[pstate_history_ptr];
	ZERO(pstate);

	// slot with previous values
	pstate_t *h1 = get_pstate_history(-1);

	int d = 0;

	pstate->pv10 = SFI(inverter10->DCW, inverter10->DCW_SF);
	d |= pstate->pv10 != h1->pv10;

	pstate->pv7 = SFI(inverter7->W, inverter7->W_SF);
	d |= pstate->pv7 != h1->pv7;

	return d;
}

static void loop() {
	int hour, day;
	device_t *device = 0;

	// initialize hourly & daily
	time_t last_ts = time(NULL), now_ts = time(NULL);
	struct tm *ltstatic = localtime(&now_ts);
	hour = ltstatic->tm_hour;
	day = ltstatic->tm_wday;

	// wait for threads to produce data
	sleep(1);

	while (1) {
		if (device)
			sleep(3); // wait for regulation to take effect
		else
			msleep(500); // wait for new values

		// get actual calendar time - and make a copy as subsequent calls to localtime() will override them
		now_ts = time(NULL);
		ltstatic = localtime(&now_ts);
		memcpy(now, ltstatic, sizeof(*ltstatic));

		// update state from modbus registers
		int delta = update();

		// evaluate response
		if (device)
			device = check_response(device);

		if (delta) {
			// calculate new state
			calculate();

			// execute regulator logic
			device = regulate();
		}

		// hourly tasks
		if (hour != now->tm_hour) {
			hour = now->tm_hour;
			hourly();
		}

		// daily tasks
		if (day != now->tm_wday) {
			day = now->tm_wday;
			daily();
		}

		// set history pointer to next slot if we had changes or regulations
		if (delta || device) {
			pstate->wait = now_ts - last_ts;
			dump_pstate_history(6);
			if (++pstate_history_ptr == PSTATE_HISTORY)
				pstate_history_ptr = 0;
			last_ts = now_ts;
		}
	}
}

static void* fronius10(void *arg) {
	int rc, errors;

	uint16_t inverter_registers[sizeof(sunspec_inverter_t)];
	inverter10 = (sunspec_inverter_t*) &inverter_registers;

	uint16_t meter_registers[sizeof(sunspec_meter_t)];
	meter = (sunspec_meter_t*) &meter_registers;

	uint16_t storage_registers[sizeof(sunspec_storage_t)];
	storage = (sunspec_storage_t*) &storage_registers;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL))
		return (void*) 0;

	while (1) {
		ZERO(inverter_registers);
		ZERO(storage_registers);
		ZERO(meter_registers);

		errors = 0;
		modbus_t *mb = modbus_new_tcp("192.168.25.231", 502);

		modbus_set_response_timeout(mb, 5, 0);
		modbus_set_error_recovery(mb, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

		// TODO remove
		rc = modbus_set_slave(mb, 2);
		if (rc == -1)
			xlog("Fronius10 invalid slave ID");

		if (modbus_connect(mb) == -1) {
			xlog("Fronius10 connection failed: %s, retry in 60sec...", modbus_strerror(errno));
			modbus_free(mb);
			sleep(60);
			continue;
		}

		while (1) {
			msleep(1000);

			// check error counter
			if (errors == 10)
				break;

			rc = modbus_read_registers(mb, INVERTER_OFFSET - 1, ARRAY_SIZE(inverter_registers), inverter_registers);
			if (rc == -1) {
				xlog("%s", modbus_strerror(errno));
				errors++;
				continue;
			}

			errors = 0;
		}

		set_all_devices(0);
		modbus_close(mb);
		modbus_free(mb);
	}

	return (void*) 0;
}

static void* fronius7(void *arg) {
	int rc, errors;

	uint16_t inverter_registers[sizeof(sunspec_inverter_t)];
	inverter7 = (sunspec_inverter_t*) &inverter_registers;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL))
		return (void*) 0;

	while (1) {
		ZERO(inverter_registers);

		errors = 0;
		modbus_t *mb = modbus_new_tcp("192.168.25.231", 502);

		modbus_set_response_timeout(mb, 5, 0);
		modbus_set_error_recovery(mb, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

		// Fronius7 is secondary unit in installation setup
		rc = modbus_set_slave(mb, 2);
		if (rc == -1)
			xlog("Fronius7 invalid slave ID");

		if (modbus_connect(mb) == -1) {
			xlog("Fronius7 connection failed: %s, retry in 60sec...", modbus_strerror(errno));
			modbus_free(mb);
			sleep(60);
			continue;
		}

		while (1) {
			msleep(1000);

			// check error counter
			if (errors == 10)
				break;

			rc = modbus_read_registers(mb, INVERTER_OFFSET - 1, ARRAY_SIZE(inverter_registers), inverter_registers);
			if (rc == -1) {
				xlog("%s", modbus_strerror(errno));
				errors++;
				continue;
			}

			errors = 0;
		}

		modbus_close(mb);
		modbus_free(mb);
	}

	return (void*) 0;
}

static int init() {
	init_all_devices();

	if (pthread_create(&thread_fronius10, NULL, &fronius10, NULL))
		return -1;

	if (pthread_create(&thread_fronius7, NULL, &fronius7, NULL))
		return -1;

	return 0;
}

static void stop() {
	pthread_cancel(thread_fronius10);
	pthread_join(thread_fronius10, NULL);

	pthread_cancel(thread_fronius7);
	pthread_join(thread_fronius7, NULL);
}

int main(int argc, char *argv[]) {
	set_debug(1);
	set_xlog(XLOG_STDOUT);

	init();
	loop();
	stop();
	return 0;
}
