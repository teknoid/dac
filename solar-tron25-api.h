#include <pthread.h>

#include "tasmota-devices.h"
#include "solar-common.h"
#include "frozen.h"
#include "curl.h"
#include "utils.h"

#define AKKU_BURNOUT			1
#define BASELOAD				(WINTER ? 300 : 200)
#define MINIMUM					(BASELOAD / 2)

#ifndef SOLAR_MAIN
#define TEMP_IN					sensors->htu21_temp
#define TEMP_OUT				sensors->sht31_temp
#endif

// devices
static device_t a1 = { .name = "akku", .total = 0, .ramp = &ramp_akku, .adj = 0 }, *AKKU = &a1;
static device_t b1 = { .name = "boiler1", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t b2 = { .name = "boiler2", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t b3 = { .name = "boiler3", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t h1 = { .id = SWITCHBOX, .r = 1, .name = "küche", .total = 500, .ramp = &ramp_heater, .adj = 0 };
static device_t h2 = { .id = SWITCHBOX, .r = 2, .name = "wozi", .total = 500, .ramp = &ramp_heater, .adj = 0 };
static device_t h3 = { .id = PLUG5, .r = 0, .name = "schlaf", .total = 500, .ramp = &ramp_heater, .adj = 0 };
static device_t h4 = { .id = SWITCHBOX, .r = 3, .name = "tisch", .total = 200, .ramp = &ramp_heater, .adj = 0 };

// all devices, needed for initialization
static device_t *DEVICES[] = { &a1, &b1, &b2, &b3, &h1, &h2, &h3, &h4, 0 };

// first charge akku, then boilers, then heaters
static device_t *DEVICES_MODEST[] = { &a1, &b1, &h1, &h2, &h3, &h4, &b2, &b3, 0 };

// steal all akku charge power
static device_t *DEVICES_GREEDY[] = { &h1, &h2, &h3, &h4, &b1, &b2, &b3, &a1, 0 };

// heaters, then akku, then boilers (catch remaining pv from secondary inverters or if akku is not able to consume all generated power)
static device_t *DEVICES_PLENTY[] = { &h1, &h2, &h3, &h4, &a1, &b1, &b2, &b3, 0 };

// force boiler heating first
static device_t *DEVICES_BOILERS[] = { &b1, &b2, &b3, &h1, &h2, &h3, &h4, &a1, 0 };
static device_t *DEVICES_BOILER1[] = { &b1, &a1, &b2, &b3, &h1, &h2, &h3, &h4, 0 };
static device_t *DEVICES_BOILER3[] = { &b3, &a1, &b1, &b2, &h1, &h2, &h3, &h4, 0 };

// define POTDs
static const potd_t MODEST = { .name = "MODEST", .devices = DEVICES_MODEST };
static const potd_t GREEDY = { .name = "GREEDY", .devices = DEVICES_GREEDY };
static const potd_t PLENTY = { .name = "PLENTY", .devices = DEVICES_PLENTY };
static const potd_t BOILERS = { .name = "BOILERS", .devices = DEVICES_BOILERS };
static const potd_t BOILER1 = { .name = "BOILER1", .devices = DEVICES_BOILER1 };
static const potd_t BOILER3 = { .name = "BOILER3", .devices = DEVICES_BOILER3 };

#define MIN_SOC					50
#define AKKU_CHARGE_MAX			4500
#define AKKU_DISCHARGE_MAX		4500
#define AKKU_CAPACITY			11000

typedef struct _raw raw_t;
struct _raw {
	float akku;
	float grid;
	float mppt1;
	float mppt2;
	float mppt3;
	float mppt4;
	float total_mppt1;
	float total_mppt2;
	float total_mppt3;
	float total_mppt4;
	float ac1;
	float dc1;
	float ac2;
	float dc2;
	float soc;
	float prod;
	float cons;
	float p1;
	float p2;
	float p3;
	float v1;
	float v2;
	float v3;
	float f;
};
static raw_t raw, *r = &raw;

// reading Inverter API: CURL handles, response memory, raw date, error counter
static CURL *curl1, *curl2;
static response_t memory = { 0 };
static pthread_t thread_update;

#define CHA						"{ channels { "
#define END						" } }"

// 393216
#define JF10MPPT1				" PV_POWERACTIVE_MEAN_01_F32:%f "
#define JF10MPPT2				" PV_POWERACTIVE_MEAN_02_F32:%f "
#define JF10TOTALMPPT1			" PV_ENERGYACTIVE_ACTIVE_SUM_01_U64:%f "
#define JF10TOTALMPPT2			" PV_ENERGYACTIVE_ACTIVE_SUM_02_U64:%f "
#define JF10F					" ACBRIDGE_FREQUENCY_MEAN_F32:%f "

// 262144
#define JF10AC					" ACBRIDGE_POWERACTIVE_SUM_MEAN_F32:%f "
#define JF10DC					" PV_POWERACTIVE_SUM_F64:%f "
#define JF10BAT					" BAT_POWERACTIVE_F64:%f "

// 16580608
#define JBSOC					" BAT_VALUE_STATE_OF_CHARGE_RELATIVE_U16:%f "

// 16252928
#define JMP						" SMARTMETER_POWERACTIVE_MEAN_SUM_F64:%f "
#define JMEC					" SMARTMETER_ENERGYACTIVE_CONSUMED_SUM_F64:%f "
#define JMEP					" SMARTMETER_ENERGYACTIVE_PRODUCED_SUM_F64:%f "
#define JMP1					" SMARTMETER_POWERACTIVE_01_F64:%f "
#define JMP2					" SMARTMETER_POWERACTIVE_02_F64:%f "
#define JMP3					" SMARTMETER_POWERACTIVE_03_F64:%f "
#define JMV1					" SMARTMETER_VOLTAGE_MEAN_01_F64:%f "
#define JMV2					" SMARTMETER_VOLTAGE_MEAN_02_F64:%f "
#define JMV3					" SMARTMETER_VOLTAGE_MEAN_03_F64:%f "
#define JMF						" SMARTMETER_FREQUENCY_MEAN_F64:%f "

// 131170
#define JF7MPPT1				" Power_DC_String_1:%f "
#define JF7MPPT2				" Power_DC_String_2:%f "
#define JF7TOTALMPPT1			" Energy_DC_String_1:%f "
#define JF7TOTALMPPT2			" Energy_DC_String_2:%f "
#define JF7AC					" PowerReal_PAC_Sum:%f "

// inverter1 is  Fronius Symo GEN24 10.0 with connected BYD Akku
#define URL_READABLE_INVERTER1	"http://fronius10/components/readable"
static int parse_inverter1(response_t *resp) {
	int ret;
	char *p;

	// workaround for accessing inverter number as key: "393216" : {
	p = strstr(resp->buffer, "\"393216\"") + 8 + 2;
	ret = json_scanf(p, strlen(p), CHA JF10MPPT1 JF10MPPT2 JF10TOTALMPPT1 JF10TOTALMPPT2 JF10F END, &r->mppt1, &r->mppt2, &r->total_mppt1, &r->total_mppt2, &r->f);
	if (ret != 5)
		return xerr("SOLAR parse_readable() warning! parsing 393216: expected 5 values but got %d", ret);

	// workaround for accessing inverter number as key: "262144" : {
	p = strstr(resp->buffer, "\"262144\"") + 8 + 2;
	ret = json_scanf(p, strlen(p), CHA JF10AC JF10DC JF10BAT END, &r->ac1, &r->dc1, &r->akku);
	if (ret != 3)
		return xerr("SOLAR parse_readable() warning! parsing 262144: expected 3 values but got %d", ret);

	// workaround for accessing akku number as key: "16580608" : {
	p = strstr(resp->buffer, "\"16580608\"") + 10 + 2;
	ret = json_scanf(p, strlen(p), CHA JBSOC END, &r->soc);
	if (ret != 1)
		return xerr("SOLAR parse_readable() warning! parsing 16580608: expected 1 values but got %d", ret);

	// workaround for accessing smartmeter number as key: "16252928" : {
	p = strstr(resp->buffer, "\"16252928\"") + 10 + 2;
	ret = json_scanf(p, strlen(p), CHA JMP JMEC JMEP JMP1 JMP2 JMP3 JMV1 JMV2 JMV3 END, &r->grid, &r->cons, &r->prod, &r->p1, &r->p2, &r->p3, &r->v1, &r->v2, &r->v3);
	if (ret != 9)
		return xerr("SOLAR parse_readable() warning! parsing 16252928: expected 9 values but got %d", ret);

	return 0;
}

// inverter2 is Fronius Symo 7.0-3-M
#define URL_READABLE_INVERTER2	"http://fronius7/components/readable"
static int parse_inverter2(response_t *resp) {
	int ret;
	char *p;

	// workaround for accessing inverter number as key: "131170" : {
	p = strstr(resp->buffer, "\"131170\"") + 8 + 2;
	ret = json_scanf(p, strlen(p), CHA JF7MPPT1 JF7MPPT2 JF7TOTALMPPT1 JF7TOTALMPPT2 JF7AC END, &r->mppt3, &r->mppt4, &r->total_mppt3, &r->total_mppt4, &r->ac2);
	if (ret != 5)
		return xerr("SOLAR parse_readable() warning! parsing 131170: expected 1 values but got %d", ret);

	return 0;
}

static void* update(void *arg) {
	int wait = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {

		if (timelock)
			wait = 0; // immediately get device response

		// wait for next second
		time_t now_ts = time(NULL);
		while (now_ts == time(NULL))
			msleep(100);

		if (wait--)
			continue;

		PROFILING_START

		// read inverter1
		curl_perform(curl1, &memory, &parse_inverter1);

		pthread_mutex_lock(&pstate_lock);

		pstate->grid = r->grid;
		pstate->mppt1 = r->mppt1;
		pstate->mppt2 = r->mppt2;
		pstate->soc = r->soc * 10.0;
		pstate->ac1 = r->ac1;
		pstate->dc1 = r->dc1;
		pstate->akku = r->akku;
		pstate->p1 = r->p1;
		pstate->p2 = r->p2;
		pstate->p3 = r->p3;
		pstate->v1 = r->v1;
		pstate->v2 = r->v2;
		pstate->v3 = r->v3;
		pstate->f = r->f * 100.0 - 5000; // store only the diff

		int offline = pstate->mppt1 < NOISE && pstate->mppt2 < NOISE;
		if (!offline) {
			// this inverter goes into sleep mode overnight - so read inverter2 only when inverter1 produces PV
			curl_perform(curl2, &memory, &parse_inverter2);
			pstate->mppt3 = r->mppt3;
			pstate->mppt4 = r->mppt4;
			pstate->ac2 = r->ac2;
			pstate->dc2 = pstate->mppt3 + pstate->mppt4; // Fronius7 has no battery - DC is PV only
		}

		pthread_mutex_unlock(&pstate_lock);

		// update counter hour 0 when empty
		if (COUNTER_0->mppt1 == 0)
			COUNTER_0->mppt1 = counter->mppt1;
		if (COUNTER_0->mppt2 == 0)
			COUNTER_0->mppt2 = counter->mppt2;
		if (COUNTER_0->mppt3 == 0)
			COUNTER_0->mppt3 = counter->mppt3;
		if (COUNTER_0->mppt4 == 0)
			COUNTER_0->mppt4 = counter->mppt4;
		if (COUNTER_0->produced == 0)
			COUNTER_0->produced = counter->produced;
		if (COUNTER_0->consumed == 0)
			COUNTER_0->consumed = counter->consumed;

		if (offline)
			wait = 300; // offline
		else
			wait = 30; // default

		// xdebug("SOLAR update ac1=%d ac2=%d grid=%d akku=%d soc=%d dc1=%d wait=%d\n", pstate->ac1, pstate->ac2, pstate->grid, pstate->akku, pstate->soc, pstate->dc1, wait);

		PROFILING_LOG("SOLAR update")
	}
}

static int solar_init() {
	// libcurl API handle for inverter1
	curl1 = curl_init(URL_READABLE_INVERTER1, &memory);
	if (curl1 == NULL)
		return xerr("Error initializing libcurl");

	// libcurl API handle for inverter2
	curl2 = curl_init(URL_READABLE_INVERTER2, &memory);
	if (curl2 == NULL)
		return xerr("Error initializing libcurl");

	// start updater thread
	if (pthread_create(&thread_update, NULL, &update, NULL))
		return xerr("Error creating thread_update");

	return 0;
}

static void solar_stop() {
	if (pthread_cancel(thread_update))
		xlog("Error canceling thread_update");

	if (pthread_join(thread_update, NULL))
		xlog("Error joining thread_update");
}

static int grid() {
	return 0; // unimplemented
}

static int battery(char *arg) {
	return 0; // unimplemented
}

static int storage_min(char *arg) {
	return 0; // unimplemented
}

static int akku_standby() {
	AKKU->state = Standby;
	AKKU->power = 0;
	return 0; // continue loop
}

static int akku_charge() {
	AKKU->state = Charge;
	AKKU->power = 1;
	return 0; // continue loop
}

static int akku_discharge() {
	AKKU->state = Discharge;
	AKKU->power = 0;
	return 0; // continue loop
}
