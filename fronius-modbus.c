#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include <signal.h>
#include <pthread.h>

#include <modbus/modbus.h>

#include "fronius-config.h"
#include "utils.h"

#define INVERTER_OFFSET		40070
#define METER_OFFSET		40070

#define SFF(x, y)			(y == 0 ? x : x * pow(10.0, y))
#define SFI(x, y)			(y == 0 ? x : (int)(x * pow(10, y)))

typedef struct sunspec_inverter_t {
	uint16_t ID;
	uint16_t L;
	uint16_t A;
	uint16_t AphA;
	uint16_t AphB;
	uint16_t AphC;
	int16_t A_SF;
	uint16_t PPVphAB;
	uint16_t PPVphBC;
	uint16_t PPVphCA;
	uint16_t PhVphA;
	uint16_t PhVphB;
	uint16_t PhVphC;
	int16_t V_SF;
	int16_t W;
	int16_t W_SF;
	uint16_t Hz;
	int16_t Hz_SF;
	int16_t VA;
	int16_t VA_SF;
	int16_t VAr;
	int16_t VAr_SF;
	int16_t PF;
	int16_t PF_SF;
	uint32_t WH;
	int16_t WH_SF;
	uint16_t DCA;
	int16_t DCA_SF;
	uint16_t DCV;
	int16_t DCV_SF;
	int16_t DCW;
	int16_t DCW_SF;
	int16_t TmpCab;
	int16_t TmpSnk;
	int16_t TmpTrns;
	int16_t TmpOt;
	int16_t Tmp_SF;
	uint16_t St;
	uint16_t StVnd;
	uint32_t Evt1;
	uint32_t Evt2;
	uint32_t EvtVnd1;
	uint32_t EvtVnd2;
	uint32_t EvtVnd3;
	uint32_t EvtVnd4;
} sunspec_inverter_t;

typedef struct sunspec_meter_t {
	uint16_t ID;
	uint16_t L;
	int16_t A;
	int16_t AphA;
	int16_t AphB;
	int16_t AphC;
	int16_t A_SF;
	int16_t PhV;
	int16_t PhVphA;
	int16_t PhVphB;
	int16_t PhVphC;
	int16_t PPV;
	int16_t PPVphAB;
	int16_t PPVphBC;
	int16_t PPVphCA;
	int16_t V_SF;
	int16_t Hz;
	int16_t Hz_SF;
	int16_t W;
	int16_t WphA;
	int16_t WphB;
	int16_t WphC;
	int16_t W_SF;
	int16_t VA;
	int16_t VAphA;
	int16_t VAphB;
	int16_t VAphC;
	int16_t VA_SF;
	int16_t VAR;
	int16_t VARphA;
	int16_t VARphB;
	int16_t VARphC;
	int16_t VAR_SF;
	int16_t PF;
	int16_t PFphA;
	int16_t PFphB;
	int16_t PFphC;
	int16_t PF_SF;
	uint32_t TotWhExp;
	uint32_t TotWhExpPhA;
	uint32_t TotWhExpPhB;
	uint32_t TotWhExpPhC;
	uint32_t TotWhImp;
	uint32_t TotWhImpPhA;
	uint32_t TotWhImpPhB;
	uint32_t TotWhImpPhC;
	int16_t TotWh_SF;
	uint32_t TotVAhExp;
	uint32_t TotVAhExpPhA;
	uint32_t TotVAhExpPhB;
	uint32_t TotVAhExpPhC;
	uint32_t TotVAhImp;
	uint32_t TotVAhImpPhA;
	uint32_t TotVAhImpPhB;
	uint32_t TotVAhImpPhC;
	int16_t TotVAh_SF;
	uint32_t TotVArhImpQ1;
	uint32_t TotVArhImpQ1phA;
	uint32_t TotVArhImpQ1phB;
	uint32_t TotVArhImpQ1phC;
	uint32_t TotVArhImpQ2;
	uint32_t TotVArhImpQ2phA;
	uint32_t TotVArhImpQ2phB;
	uint32_t TotVArhImpQ2phC;
	uint32_t TotVArhExpQ3;
	uint32_t TotVArhExpQ3phA;
	uint32_t TotVArhExpQ3phB;
	uint32_t TotVArhExpQ3phC;
	uint32_t TotVArhExpQ4;
	uint32_t TotVArhExpQ4phA;
	uint32_t TotVArhExpQ4phB;
	uint32_t TotVArhExpQ4phC;
	int16_t TotVArh_SF;
	uint32_t Evt;
} sunspec_meter_t;

typedef struct sunspec_storage_t {
	uint16_t ID;
	uint16_t L;
	uint16_t WchaMax;
	uint16_t WchaGra;
	uint16_t WdisChaGra;
	uint16_t StorCtl_Mod;
	uint16_t VAChaMax;
	uint16_t MinRsvPct;
	uint16_t ChaState;
	uint16_t StorAval;
	uint16_t InBatV;
	uint16_t ChaSt;
	int16_t OutWRte;
	int16_t InWRte;
	uint16_t InOutWRte_WinTms;
	uint16_t InOutWRte_RvrtTms;
	uint16_t InOutWRte_RmpTms;
	uint16_t ChaGriSet;
	int16_t WchaMax_SF;
	int16_t WchaDisChaGra_SF;
	int16_t VAChaMax_SF;
	int16_t MinRsvPct_SF;
	int16_t ChaState_SF;
	int16_t StorAval_SF;
	int16_t InBatV_SF;
	int16_t InOutWRte_SF;
} sunspec_storage_t;

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

static void dump(uint16_t registers[], size_t size) {
	for (int i = 0; i < size; i++)
		printf("reg[%d]=%05d (0x%04X)\n", i, registers[i], registers[i]);
}

static void set_all_devices(int power) {
}

static void regulate() {
	printf("PhVphA %d (%2.1f)\n", SFI(inverter7->PhVphA, inverter7->V_SF), SFF(inverter7->PhVphA, inverter7->V_SF));
	printf("PhVphB %d (%2.1f)\n", SFI(inverter7->PhVphB, inverter7->V_SF), SFF(inverter7->PhVphB, inverter7->V_SF));
	printf("PhVphC %d (%2.1f)\n", SFI(inverter7->PhVphC, inverter7->V_SF), SFF(inverter7->PhVphC, inverter7->V_SF));

	printf("DCW    %d (%2.1f)\n", SFI(inverter10->DCW, inverter7->DCW_SF), SFF(inverter10->DCW, inverter10->DCW_SF));
	printf("W      %d (%2.1f)\n", SFI(inverter10->W, inverter7->W_SF), SFF(inverter10->W, inverter10->W_SF));

	printf("PV7=%d\n", state->pv7);
}

static void takeover() {
	// clear slot in history for storing new state
	state = &state_history[state_history_ptr];
	ZERO(state);

	state->pv7 = SFI(inverter7->W, inverter7->W_SF);

	// set history pointer to next slot
	if (++state_history_ptr == HISTORY)
		state_history_ptr = 0;
}

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
	while (1) {
		msleep(500);

		// do delta check and execute regulator logic
		int delta = check_delta();
		if (delta) {
			takeover();
			regulate();
		}
	}
}

void* fronius10(void *arg) {
	int rc, errors;
	modbus_t *mb;

	uint16_t inverter_registers[sizeof(sunspec_inverter_t)];
	inverter10 = (sunspec_inverter_t*) &inverter_registers;

	uint16_t meter_registers[sizeof(sunspec_meter_t)];
	meter = (sunspec_meter_t*) &meter_registers;

	uint16_t storage_registers[sizeof(sunspec_storage_t)];
	storage = (sunspec_storage_t*) &storage_registers;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL))
		return (void*) 0;

	while (1) {
		errors = 0;
		mb = modbus_new_tcp("192.168.25.231", 502);

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

void* fronius7(void *arg) {
	int rc, errors;
	modbus_t *mb;

	uint16_t inverter_registers[sizeof(sunspec_inverter_t)];
	inverter7 = (sunspec_inverter_t*) &inverter_registers;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL))
		return (void*) 0;

	while (1) {
		errors = 0;
		mb = modbus_new_tcp("192.168.25.231", 502);

		modbus_set_response_timeout(mb, 5, 0);
		modbus_set_error_recovery(mb, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

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
