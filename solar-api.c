#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include "solar-common.h"
#include "sensors.h"
#include "frozen.h"
#include "utils.h"
#include "curl.h"
#include "mcp.h"

#define MIN_SOC					50
#define AKKU_CHARGE_MAX			4500
#define AKKU_DISCHARGE_MAX		4500
#define AKKU_CAPACITY			11000

#ifndef TEMP_IN
#define TEMP_IN					22.0
#endif

#ifndef TEMP_OUT
#define TEMP_OUT				15.0
#endif

#define URL_READABLE_INVERTER1	"http://fronius10/components/readable"
#define URL_READABLE_INVERTER2	"http://fronius7/components/readable"

#define CHA						"{ channels { "
#define END						" } }"

// 393216
#define JF10MPPT1				" PV_POWERACTIVE_MEAN_01_F32:%f "
#define JF10MPPT2				" PV_POWERACTIVE_MEAN_02_F32:%f "
#define JF10MPPT1SUM			" PV_ENERGYACTIVE_ACTIVE_SUM_01_U64:%f "
#define JF10MPPT2SUM			" PV_ENERGYACTIVE_ACTIVE_SUM_02_U64:%f "
#define JF10AC					" ACBRIDGE_POWERACTIVE_SUM_MEAN_F32:%f "
#define JF10F					" ACBRIDGE_FREQUENCY_MEAN_F32:%f "
#define JF10BAT					" BAT_POWERACTIVE_MEAN_F32:%f "

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
#define JF7MPPT1SUM				" Energy_DC_String_1:%f "
#define JF7MPPT2SUM				" Energy_DC_String_2:%f "
#define JF7AC					" PowerReal_PAC_Sum:%f "

typedef struct _raw raw_t;
struct _raw {
	float akku;
	float grid;
	float mppt1;
	float mppt2;
	float mppt3;
	float mppt4;
	float mppt1_total;
	float mppt2_total;
	float mppt3_total;
	float mppt4_total;
	float ac1;
	float ac2;
	float dc2;
	float soc;
	float cons;
	float prod;
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

int temp_in() {
	return TEMP_IN;
}

int temp_out() {
	return TEMP_OUT;
}

int akku_capacity() {
	return AKKU_CAPACITY;
}

int akku_get_min_soc() {
	return MIN_SOC;
}

void akku_set_min_soc(int min) {
	// unimplemented
}

int akku_charge_max() {
	return AKKU_CHARGE_MAX;
}

int akku_discharge_max() {
	return AKKU_DISCHARGE_MAX;
}

int akku_standby(device_t *akku) {
	// dummy implementation
	akku->state = Standby;
	akku->power = 0;
	return 0;
}

int akku_charge(device_t *akku, int limit) {
	// dummy implementation
	akku->state = Charge;
	akku->power = 1;
	return 0;
}

int akku_discharge(device_t *akku, int limit) {
	// dummy implementation
	akku->state = Discharge;
	akku->power = 0;
	return 0;
}

void inverter_status(int *inv1, int *inv2) {
	// unimplemented
}

// inverter1 is  Fronius Symo GEN24 10.0 with connected BYD Akku
static int parse_inverter1(response_t *resp) {
	int ret;
	char *p;

	// workaround for accessing inverter number as key: "0" : {
	p = strstr(resp->buffer, "\"0\"") + 8 + 2;
	ret = json_scanf(p, strlen(p), CHA JF10MPPT1 JF10MPPT2 JF10MPPT1SUM JF10MPPT2SUM JF10AC JF10F JF10BAT END, &r->mppt1, &r->mppt2, &r->mppt1_total, &r->mppt2_total, &r->ac1,
			&r->f, &r->akku);
	if (ret != 7)
		return xerr("SOLAR parse_inverter1() warning! parsing 0: expected 7 values but got %d", ret);

	// workaround for accessing akku number as key: "16580608" : {
	p = strstr(resp->buffer, "\"16580608\"") + 10 + 2;
	ret = json_scanf(p, strlen(p), CHA JBSOC END, &r->soc);
	if (ret != 1)
		return xerr("SOLAR parse_inverter1() warning! parsing 16580608: expected 1 values but got %d", ret);

	// workaround for accessing smartmeter number as key: "16252928" : {
	p = strstr(resp->buffer, "\"16252928\"") + 10 + 2;
	ret = json_scanf(p, strlen(p), CHA JMP JMEC JMEP JMP1 JMP2 JMP3 JMV1 JMV2 JMV3 END, &r->grid, &r->cons, &r->prod, &r->p1, &r->p2, &r->p3, &r->v1, &r->v2, &r->v3);
	if (ret != 9)
		return xerr("SOLAR parse_inverter1() warning! parsing 16252928: expected 9 values but got %d", ret);

	return 0;
}

// inverter2 is Fronius Symo 7.0-3-M
static int parse_inverter2(response_t *resp) {
	int ret;
	char *p;

	// workaround for accessing inverter number as key: "131170" : {
	p = strstr(resp->buffer, "\"131170\"") + 8 + 2;
	ret = json_scanf(p, strlen(p), CHA JF7MPPT1 JF7MPPT2 JF7MPPT1SUM JF7MPPT2SUM JF7AC END, &r->mppt3, &r->mppt4, &r->mppt3_total, &r->mppt4_total, &r->ac2);
	if (ret != 5)
		return xerr("SOLAR parse_inverter2() warning! parsing 131170: expected 5 values but got %d", ret);

	return 0;
}

static void loop() {
	int wait = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	while (1) {
		WAIT_NEXT_SECOND

		// skip wait to get fresh pstate data
		if (pstate->grid > NOISE)
			wait = 0;

		// wait
		if (wait--)
			continue;

		PROFILING_START

		// read inverter1
		curl_perform(curl1, &memory, &parse_inverter1);

		// inverter2 goes into sleep mode overnight - so read only when inverter1 produces PV
		int offline = r->mppt1 < NOISE && r->mppt2 < NOISE;
		if (!offline) {
			curl_perform(curl2, &memory, &parse_inverter2);
			// TODO prüfen - geht scheinbar wieder
			// !!! no regular updates on fields Energy_DC_String_X - so use PowerReal_PAC_Sum for all DC/AC values
			// r->mppt3 = r->dc2 = r->ac2;
			// r->mppt4 = 0;
			r->dc2 = r->mppt3 + r->mppt4;
		} else
			// reset pstates, keep counters
			r->mppt3 = r->mppt4 = r->ac2 = r->dc2 = 0;

		pthread_mutex_lock(&collector_lock);

		pstate->ac1 = r->ac1;
		pstate->dc1 = r->mppt1 + r->mppt2 + r->akku;
		pstate->mppt1 = r->mppt1;
		pstate->mppt2 = r->mppt2;

		pstate->ac2 = r->ac2;
		pstate->dc2 = r->dc2;
		pstate->mppt3 = r->mppt3;
		pstate->mppt4 = r->mppt4;

		pstate->grid = r->grid;
		pstate->akku = r->akku;
		pstate->soc = r->soc * 10.0; // store x10 scaled
		pstate->p1 = r->p1;
		pstate->p2 = r->p2;
		pstate->p3 = r->p3;
		pstate->v1 = r->v1;
		pstate->v2 = r->v2;
		pstate->v3 = r->v3;
		pstate->f = r->f * 100.0 - 5000; // store only the diff

		CM_NOW->mppt1 = r->mppt1_total / 3600; // Watt-seconds
		CM_NOW->mppt2 = r->mppt2_total / 3600; // Watt-seconds
		CM_NOW->mppt3 = r->mppt3_total;
		CM_NOW->mppt4 = r->mppt4_total;
		CM_NOW->consumed = r->cons;
		CM_NOW->produced = r->prod;

		pthread_mutex_unlock(&collector_lock);

		// update NULL counter if empty
		if (CM_NULL->mppt1 == 0)
			CM_NULL->mppt1 = CM_NOW->mppt1;
		if (CM_NULL->mppt2 == 0)
			CM_NULL->mppt2 = CM_NOW->mppt2;
		if (CM_NULL->mppt3 == 0)
			CM_NULL->mppt3 = CM_NOW->mppt3;
		if (CM_NULL->mppt4 == 0)
			CM_NULL->mppt4 = CM_NOW->mppt4;
		if (CM_NULL->produced == 0)
			CM_NULL->produced = CM_NOW->produced;
		if (CM_NULL->consumed == 0)
			CM_NULL->consumed = CM_NOW->consumed;

		// xdebug("SOLAR pstate update ac1=%d ac2=%d grid=%d akku=%d soc=%d dc1=%d wait=%d", pstate->ac1, pstate->ac2, pstate->grid, pstate->akku, pstate->soc, pstate->dc1, wait);
		// xdebug("SOLAR counter update mppt1=%d mppt2=%d mppt3=%d mppt4=%d cons=%d prod=%d", cm->mppt1, cm->mppt2, cm->mppt3, cm->mppt4, cm->consumed, cm->produced);

		// offline / default
		wait = offline ? 300 : 30;

		PROFILING_LOG("SOLAR update")
	}
}

// Kalibrierung über SmartMeter mit Laptop im Akku-Betrieb:
// - Nur Nachts
// - Akku aus
// - Külschränke aus
// - Heizung aus
// - Rechner aus
static int calibrate(char *name) {
	const char *addr = resolve_ip(name);
	char message[16];
	int voltage, closest, target;
	float offset_start = 0, offset_end = 0;
	int measure[1000], raster[101];

	// create a socket
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	// write IP and port into sockaddr structure
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	CURL *curl = curl_init(URL_READABLE_INVERTER1, &memory);
	if (curl == NULL)
		perror("Error initializing libcurl");

	printf("starting calibration on %s (%s)\n", name, addr);
	snprintf(message, 16, "v:0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// average offset power at start
	printf("calculating offset start");
	for (int i = 0; i < 10; i++) {
		curl_perform(curl, &memory, &parse_inverter1);
		offset_start += r->grid;
		printf(" %.1f", r->grid);
		sleep(1);
	}
	offset_start /= 10;
	printf(" --> average %.1f\n", offset_start);

	printf("waiting for heat up 100%%...\n");
	snprintf(message, 16, "v:10000:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// get maximum power
	curl_perform(curl, &memory, &parse_inverter1);
	int max_power = round100(r->grid - offset_start);

	int onepercent = max_power / 100;
	printf("starting measurement with maximum power %d watt 1%%=%d watt\n", max_power, onepercent);

	// do a full drive over SSR characteristic load curve from 10 down to 0 volt and capture power
	for (int i = 0; i < 1000; i++) {
		voltage = 10000 - (i * 10);

		snprintf(message, 16, "v:%d:%d", voltage, 0);
		sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));

		// give SSR time to set voltage and smart meter to measure
		if (2000 < voltage && voltage < 8000)
			usleep(1000 * 1000); // more time between 8 and 2 volts
		else
			usleep(1000 * 600);

		curl_perform(curl, &memory, &parse_inverter1);
		measure[i] = r->grid - offset_start;
		printf("%5d %5d\n", voltage, measure[i]);
	}

	// build raster table
	raster[0] = 10000;
	raster[100] = 0;
	for (int i = 1; i < 100; i++) {

		// calculate next target power -i%
		target = max_power - (onepercent * i);

		// find closest power to target power
		int min_diff = max_power;
		for (int j = 0; j < 1000; j++) {
			int diff = abs(measure[j] - target);
			if (diff < min_diff) {
				min_diff = diff;
				closest = j;
			}
		}

		// find all closest voltages that match target power
		int sum = 0, count = 0;
		printf("closest voltages to target power %5d matching %5d: ", target, measure[closest]);
		for (int j = 0; j < 1000; j++)
			if (measure[j] == measure[closest]) {
				printf("%5d", j);
				sum += 10000 - (j * 10);
				count++;
			}

		// average of all closest voltages
		raster[i] = sum / count;

		printf(" --> average %5d\n", raster[i]);
	}

	// average offset power at end
	printf("calculating offset end");
	for (int i = 0; i < 10; i++) {
		curl_perform(curl, &memory, &parse_inverter1);
		offset_end += r->grid;
		printf(" %.1f", r->grid);
		sleep(1);
	}
	offset_end /= 10;
	printf(" --> average %.1f\n", offset_end);

	// validate - values in measure table should shrink, not grow
	for (int i = 1; i < 1000; i++)
		if (measure[i - 1] < (measure[i] - 5)) { // with 5 watt tolerance
			int v_x = 10000 - (i * 10);
			int m_x = measure[i - 1];
			int v_y = 10000 - ((i - 1) * 10);
			int m_y = measure[i];
			printf("!!! WARNING !!! measuring tainted with parasitic power at voltage %d:%d < %d:%d\n", v_x, m_x, v_y, m_y);
		}
	if ((int) offset_start != (int) offset_end)
		printf("!!! WARNING !!! measuring tainted with parasitic power between start and end\n");

	// dump raster table in ascending order
	printf("phase angle voltage table 0..100%% in %d watt steps:\n\n", onepercent);
	printf("%d, ", raster[100]);
	for (int i = 99; i >= 0; i--) {
		printf("%d, ", raster[i]);
		if (i % 10 == 0)
			printf("\\\n");
	}

	// cleanup
	close(sock);
	curl_easy_cleanup(curl);
	return 0;
}

// sample grid values from meter
static int grid() {
	pstate_t pp, *p = &pp;

	CURL *curl = curl_init(URL_READABLE_INVERTER1, &memory);
	if (curl == NULL)
		perror("Error initializing libcurl");

	while (1) {
		msleep(666);
		curl_perform(curl, &memory, &parse_inverter1);

		p->grid = r->grid;
		p->p1 = r->p1;
		p->p2 = r->p2;
		p->p3 = r->p3;
		p->v1 = r->v1;
		p->v2 = r->v2;
		p->v3 = r->v3;
		p->f = r->f * 100.0;

		printf("%5d W  |  %4d W  %4d W  %4d W  |  %d V  %d V  %d V  |  %5.2f Hz\n", p->grid, p->p1, p->p2, p->p3, p->v1, p->v2, p->v3, FLOAT100(p->f));
	}
	return 0;
}

static int init() {

	ZERO(raw);

	// libcurl handle for inverter1 API
	curl1 = curl_init(URL_READABLE_INVERTER1, &memory);
	if (curl1 == NULL)
		return xerr("Error initializing libcurl");

	// libcurl handle for inverter2 API
	curl2 = curl_init(URL_READABLE_INVERTER2, &memory);
	if (curl2 == NULL)
		return xerr("Error initializing libcurl");

	// do not continue before we have SoC value from Fronius10
	int retry = 100;
	while (--retry) {
		msleep(100);
		if (r->soc > 1.0)
			break;
	}
	if (!retry)
		return xerr("No SoC from Fronius10");
	xdebug("SOLAR Fronius10 ready for main loop after retry=%d", retry);

	return 0;
}

static void stop() {
	curl_easy_cleanup(curl1);
	curl_easy_cleanup(curl2);
}

static int test() {
	return 0;
}

int solar_main(int argc, char **argv) {
	int c;
	while ((c = getopt(argc, argv, "c:glt")) != -1) {
		// printf("getopt %c\n", c);
		switch (c) {
		case 'c':
			// execute as: stdbuf -i0 -o0 -e0 ./solar -c boiler1 > boiler1.txt
			return calibrate(optarg);
		case 'g':
			return grid();
		case 'l':
			return mcp_main(argc, argv);
		case 't':
			return test();
		default:
			xlog("unknown getopt %c", c);
		}
	}

	return 0;
}

#ifdef SOLAR_MAIN
int main(int argc, char **argv) {
	return solar_main(argc, argv);
}
#endif

MCP_REGISTER(solar, 10, &init, &stop, &loop);
