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
static state_t state_history[HISTORY];
static state_t *state = state_history;
static int state_history_ptr = 0;

// Fronius modbus register banks
static sunspec_inverter_t *inverter10;
static sunspec_inverter_t *inverter7;
static sunspec_storage_t *storage;
static sunspec_meter_t *meter;

// Fronius modbus reader threads
static pthread_t thread_fronius10, thread_fronius7;

int set_heater(device_t *heater, int power) {
	return 0;
}

int set_boiler(device_t *boiler, int power) {
	return 0;
}

static state_t* get_state_history(int offset) {
	int i = state_history_ptr + offset;
	if (i < 0)
		i += HISTORY;
	if (i >= HISTORY)
		i -= HISTORY;
	return &state_history[i];
}

//static void dump(uint16_t registers[], size_t size) {
//	for (int i = 0; i < size; i++)
//		printf("reg[%d]=%05d (0x%04X)\n", i, registers[i], registers[i]);
//}

static void set_all_devices(int power) {
}

static void daily() {
	printf("executing daily tasks...");
}

static void hourly() {
	printf("executing hourly tasks...");
}

static void regulate() {
	printf("PhVphA %d (%2.1f)\n", SFI(inverter7->PhVphA, inverter7->V_SF), SFF(inverter7->PhVphA, inverter7->V_SF));
	printf("PhVphB %d (%2.1f)\n", SFI(inverter7->PhVphB, inverter7->V_SF), SFF(inverter7->PhVphB, inverter7->V_SF));
	printf("PhVphC %d (%2.1f)\n", SFI(inverter7->PhVphC, inverter7->V_SF), SFF(inverter7->PhVphC, inverter7->V_SF));

	printf("DCW    %d (%2.1f)\n", SFI(inverter10->DCW, inverter10->DCW_SF), SFF(inverter10->DCW, inverter10->DCW_SF));
	printf("W      %d (%2.1f)\n", SFI(inverter10->W, inverter10->W_SF), SFF(inverter10->W, inverter10->W_SF));

	printf("PV10=%d PV7=%d\n", state->pv10, state->pv7);
}

static void takeover() {
	// clear slot in history for storing new state
	state = &state_history[state_history_ptr];
	ZERO(state);

	state->pv10 = SFI(inverter10->DCW, inverter10->DCW_SF);
	state->pv7 = SFI(inverter7->W, inverter7->W_SF);

	// set history pointer to next slot
	if (++state_history_ptr == HISTORY)
		state_history_ptr = 0;
}

// TODO performance: make makro
static int delta(int v, int v_old, int d_proz) {
	int v_diff = v - v_old;
	int v_proz = v == 0 ? 0 : (v_diff * 100) / v;
	// printf("v_old=%d v=%d v_diff=%d v_proz=%d d_proz=%d\n", v_old, v, v_diff, v_proz, d_proz);
	if (abs(v_proz) >= d_proz)
		return 1;
	return 0;
}

static int check_delta() {
	state_t *h1 = get_state_history(-1);

	if (delta(SFI(inverter7->W, inverter7->W_SF), h1->pv7, 2))
		return 1;

	return 0;
}

static void loop() {
	int hour, day;

	// initialize hourly & daily
	time_t now_ts = time(NULL);
	struct tm *now = localtime(&now_ts);
	hour = now->tm_hour;
	day = now->tm_wday;

	// wait for threads to produce data
	sleep(1);

	while (1) {
		msleep(200);

		// do delta check and execute regulator logic if values have changed
		int delta = check_delta();
		if (delta) {
			takeover();
			regulate();
		}

		// update current date+time
		now_ts = time(NULL);
		now = localtime(&now_ts);

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
			fprintf(stderr, "Fronius10 invalid slave ID\n");

		if (modbus_connect(mb) == -1) {
			fprintf(stderr, "Fronius10 connection failed: %s, retry in 60sec...\n", modbus_strerror(errno));
			modbus_free(mb);
			sleep(60);
			continue;
		}

		while (1) {
			msleep(500);

			// check error counter
			if (errors == 10)
				break;

			rc = modbus_read_registers(mb, INVERTER_OFFSET - 1, ARRAY_SIZE(inverter_registers), inverter_registers);
			if (rc == -1) {
				fprintf(stderr, "%s\n", modbus_strerror(errno));
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
			fprintf(stderr, "Fronius7 invalid slave ID\n");

		if (modbus_connect(mb) == -1) {
			fprintf(stderr, "Fronius7 connection failed: %s, retry in 60sec...\n", modbus_strerror(errno));
			modbus_free(mb);
			sleep(60);
			continue;
		}

		while (1) {
			msleep(500);

			// check error counter
			if (errors == 10)
				break;

			rc = modbus_read_registers(mb, INVERTER_OFFSET - 1, ARRAY_SIZE(inverter_registers), inverter_registers);
			if (rc == -1) {
				fprintf(stderr, "%s\n", modbus_strerror(errno));
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
	for (int i = 0; i < ARRAY_SIZE(devices); i++)
		printf("%s\n", devices[i]->name);

	if (pthread_create(&thread_fronius10, NULL, &fronius10, NULL))
		return -1;

	if (pthread_create(&thread_fronius7, NULL, &fronius7, NULL))
		return -1;

	return 0;
}

static void stop() {
	printf("terminating...\n");

	pthread_cancel(thread_fronius10);
	pthread_join(thread_fronius10, NULL);

	pthread_cancel(thread_fronius7);
	pthread_join(thread_fronius7, NULL);
}

int main(int argc, char *argv[]) {
	init();
	loop();
	stop();
	return 0;
}
