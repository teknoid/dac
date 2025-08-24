// localmain
// gcc -DSOLAR_MAIN -I./include -o solar mcp.c solar-modbus.c solar-collector.c solar-dispatcher.c utils.c mosmix.c sunspec.c -lmodbus -lm

// loop
// gcc -DMCP -I./include -o solar mcp.c solar-modbus.c solar-collector.c solar-dispatcher.c utils.c mosmix.c sunspec.c sensors.c i2c.c -lmodbus -lm

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include "solar-common.h"
#include "sunspec.h"
#include "utils.h"
#include "mcp.h"

#define MIN_SOC					(inverter1 && inverter1->storage ? SFI(inverter1->storage->MinRsvPct, inverter1->storage->MinRsvPct_SF) * 10 : 0)
#define AKKU_CHARGE_MAX			(inverter1 && inverter1->nameplate ? SFI(inverter1->nameplate->MaxChaRte, inverter1->nameplate->MaxChaRte_SF) / 2 : 0)
#define AKKU_DISCHARGE_MAX		(inverter1 && inverter1->nameplate ? SFI(inverter1->nameplate->MaxDisChaRte, inverter1->nameplate->MaxDisChaRte_SF) / 2 : 0)
#define AKKU_CAPACITY			(inverter1 && inverter1->nameplate ? SFI(inverter1->nameplate->WHRtg, inverter1->nameplate->WHRtg_SF) : 0)

#ifdef MCP
#include "sensors.h"
#define TEMP_IN					sensors->htu21_temp
#define TEMP_OUT				sensors->sht31_temp
#endif

#ifndef TEMP_IN
#define TEMP_IN					22.0
#endif

#ifndef TEMP_OUT
#define TEMP_OUT				15.0
#endif

// sunspec devices
static sunspec_t *inverter1 = 0, *inverter2 = 0, *meter = 0;

int temp_in() {
	return TEMP_IN * 10; // scaled as x10
}

int temp_out() {
	return TEMP_OUT * 10; // scaled as x10
}

int akku_capacity() {
	return AKKU_CAPACITY;
}

int akku_min_soc() {
	return MIN_SOC;
}

int akku_charge_max() {
	return AKKU_CHARGE_MAX;
}

int akku_discharge_max() {
	return AKKU_DISCHARGE_MAX;
}

int akku_standby(device_t *akku) {
	akku->state = Standby;
	akku->power = 0;

	if (!sunspec_storage_limit_both(inverter1, 0, 0))
		xdebug("SOLAR set akku STANDBY");
	return 0; // continue loop
}

int akku_charge(device_t *akku) {
	akku->state = Charge;
	akku->power = 1;

	int limit = GSTATE_SUMMER || gstate->today > AKKU_CAPACITY * 2;
	if (limit) {
		if (!sunspec_storage_limit_both(inverter1, 1750, 0)) {
			xdebug("SOLAR set akku CHARGE limit 2000");
			return WAIT_AKKU_CHARGE; // loop done
		}
	} else {
		if (!sunspec_storage_limit_discharge(inverter1, 0)) {
			xdebug("SOLAR set akku CHARGE");
			return WAIT_AKKU_CHARGE; // loop done
		}
	}
	return 0; // continue loop
}

int akku_discharge(device_t *akku) {
	akku->state = Discharge;
	akku->power = 0;

	// minimum SOC: standard 5%, winter and tomorrow not much PV expected 10%
	int min_soc = GSTATE_WINTER && gstate->tomorrow < AKKU_CAPACITY && gstate->soc > 111 ? 10 : 5;
	sunspec_storage_minimum_soc(inverter1, min_soc);

	int limit = GSTATE_WINTER && (gstate->survive < 0 || gstate->tomorrow < AKKU_CAPACITY);
	if (limit) {
		if (!sunspec_storage_limit_both(inverter1, 0, BASELOAD)) {
			xdebug("SOLAR set akku DISCHARGE limit BASELOAD");
			return WAIT_RESPONSE; // loop done
		}
	} else {
		if (!sunspec_storage_limit_charge(inverter1, 0)) {
			xdebug("SOLAR set akku DISCHARGE");
			return WAIT_RESPONSE; // loop done
		}
	}
	return 0; // continue loop
}

void inverter_status(int *inv1, int *inv2) {
	*inv1 = *inv2 = 0;
	if (inverter1 && inverter1->inverter)
		*inv1 = inverter1->inverter->St;
	if (inverter2 && inverter2->inverter)
		*inv2 = inverter2->inverter->St;
}

// inverter1 is Fronius Symo GEN24 10.0 with connected BYD Akku
static void update_inverter1(sunspec_t *ss) {
	pthread_mutex_lock(&collector_lock);

	pstate->f = ss->inverter->Hz - 5000; // store only the diff
	pstate->ac1 = SFI(ss->inverter->W, ss->inverter->W_SF);
	pstate->dc1 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);
	pstate->v1 = SFI(ss->inverter->PhVphA, ss->inverter->V_SF);
	pstate->v2 = SFI(ss->inverter->PhVphB, ss->inverter->V_SF);
	pstate->v3 = SFI(ss->inverter->PhVphC, ss->inverter->V_SF);
	pstate->soc = SFF(ss->storage->ChaState, ss->storage->ChaState_SF) * 10;

	switch (ss->inverter->St) {
	case I_STATUS_OFF:
		pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = pstate->akku = 0;
		ss->active = 0;
		break;

	case I_STATUS_STARTING:
		// TODO prüfen ob Werte stimmen
		// pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = pstate->akku = 0;
		break;

	case I_STATUS_MPPT:
		pstate->mppt1 = SFI(ss->mppt->m1_DCW, ss->mppt->DCW_SF);
		pstate->mppt2 = SFI(ss->mppt->m2_DCW, ss->mppt->DCW_SF);
		// TODO find sunspec register
		pstate->akku = pstate->dc1 - (pstate->mppt1 + pstate->mppt2); // akku power is DC power minus PV

		CM_NOW->mppt1 = SFUI(ss->mppt->m1_DCWH, ss->mppt->DCWH_SF);
		CM_NOW->mppt2 = SFUI(ss->mppt->m2_DCWH, ss->mppt->DCWH_SF);

		// dissipation
		// int dissipation = pstate->dc1 - pstate->ac1 - pstate->akku;
		// xlog("SOLAR Inverter1 dissipation=%d", dissipation);

		// update NULL counter if empty
		if (CM_NULL->mppt1 == 0)
			CM_NULL->mppt1 = CM_NOW->mppt1;
		if (CM_NULL->mppt2 == 0)
			CM_NULL->mppt2 = CM_NOW->mppt2;

		ss->sleep = 0;
		ss->active = 1;
		break;

	case I_STATUS_SLEEPING:
		// let the inverter sleep
		pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = pstate->akku = 0;
		ss->sleep = SLEEP_TIME_SLEEPING;
		ss->active = 0;
		break;

	case I_STATUS_FAULT:
		xlog("SOLAR %s inverter St=%d Evt1=%d Evt2=%d", ss->name, ss->inverter->St, ss->inverter->Evt1, ss->inverter->Evt2);
		// this is normal when we are offline
		if (PSTATE_OFFLINE)
			ss->sleep = SLEEP_TIME_SLEEPING;
		ss->active = 0;
		break;

	default:
		xdebug("SOLAR %s inverter St=%d W=%d DCW=%d ", ss->name, ss->inverter->St, ss->inverter->W, ss->inverter->DCW);
		// ss->sleep = SLEEP_TIME_FAULT;
		ss->active = 0;
	}

	pthread_mutex_unlock(&collector_lock);
}

// inverter2 is Fronius Symo 7.0-3-M
static void update_inverter2(sunspec_t *ss) {
	pthread_mutex_lock(&collector_lock);

	pstate->ac2 = SFI(ss->inverter->W, ss->inverter->W_SF);
	pstate->dc2 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);

	switch (ss->inverter->St) {
	case I_STATUS_OFF:
		pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = pstate->akku = 0;
		ss->active = 0;
		break;

	case I_STATUS_STARTING:
		pstate->ac2 = pstate->dc2 = pstate->mppt3 = pstate->mppt4 = 0;
		ss->active = 0;
		break;

	case I_STATUS_MPPT:
		pstate->mppt3 = SFI(ss->mppt->m1_DCW, ss->mppt->DCW_SF);
		pstate->mppt4 = SFI(ss->mppt->m2_DCW, ss->mppt->DCW_SF);

		CM_NOW->mppt3 = SFUI(ss->mppt->m1_DCWH, ss->mppt->DCWH_SF);
		CM_NOW->mppt4 = SFUI(ss->mppt->m2_DCWH, ss->mppt->DCWH_SF);

		// dissipation
		// int dissipation = pstate->dc1 - pstate->ac1;
		// xlog("SOLAR Inverter2 dissipation=%d", dissipation);

		// update NULL counter if empty
		if (CM_NULL->mppt3 == 0)
			CM_NULL->mppt3 = CM_NOW->mppt3;
		if (CM_NULL->mppt4 == 0)
			CM_NULL->mppt4 = CM_NOW->mppt4;

		ss->sleep = 0;
		ss->active = 1;
		break;

	case I_STATUS_SLEEPING:
		// let the inverter sleep
		pstate->ac2 = pstate->dc2 = pstate->mppt3 = pstate->mppt4 = 0;
		ss->sleep = SLEEP_TIME_SLEEPING;
		ss->active = 0;
		break;

	case I_STATUS_FAULT:
		xlog("SOLAR %s inverter St=%d Evt1=%d Evt2=%d", ss->name, ss->inverter->St, ss->inverter->Evt1, ss->inverter->Evt2);
		// this is normal when we are offline
		if (PSTATE_OFFLINE)
			ss->sleep = SLEEP_TIME_SLEEPING;
		ss->active = 0;
		break;

	default:
		xdebug("SOLAR %s inverter St=%d W=%d DCW=%d ", ss->name, ss->inverter->St, ss->inverter->W, ss->inverter->DCW);
		// ss->sleep = SLEEP_TIME_FAULT;
		ss->active = 0;
	}

	pthread_mutex_unlock(&collector_lock);
}

// meter is Fronius Smart Meter TS 65A-3
static void update_meter(sunspec_t *ss) {
	pthread_mutex_lock(&collector_lock);

	pstate->grid = SFI(ss->meter->W, ss->meter->W_SF);
	pstate->p1 = SFI(ss->meter->WphA, ss->meter->W_SF);
	pstate->p2 = SFI(ss->meter->WphB, ss->meter->W_SF);
	pstate->p3 = SFI(ss->meter->WphC, ss->meter->W_SF);
//	pstate->v1 = SFI(ss->meter->PhVphA, ss->meter->V_SF);
//	pstate->v2 = SFI(ss->meter->PhVphB, ss->meter->V_SF);
//	pstate->v3 = SFI(ss->meter->PhVphC, ss->meter->V_SF);
//	pstate->f = ss->meter->Hz - 5000; // store only the diff

	CM_NOW->consumed = SFUI(ss->meter->TotWhImp, ss->meter->TotWh_SF);
	CM_NOW->produced = SFUI(ss->meter->TotWhExp, ss->meter->TotWh_SF);

	// update NULL counter if empty
	if (CM_NULL->produced == 0)
		CM_NULL->produced = CM_NOW->produced;
	if (CM_NULL->consumed == 0)
		CM_NULL->consumed = CM_NOW->consumed;

	pthread_mutex_unlock(&collector_lock);
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
	int grid, voltage, closest, target;
	int offset_start = 0, offset_end = 0;
	int measure[1000], raster[101];

	// create a sunspec handle and remove models not needed
	sunspec_t *ss = sunspec_init("fronius10", 200);
	sunspec_read(ss);
	ss->common = 0;
	ss->storage = 0;

	// create a socket
	int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	// write IP and port into sockaddr structure
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	printf("starting calibration on %s (%s)\n", name, addr);
	snprintf(message, 16, "v:0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// get maximum power, calculate 1%
	//	printf("waiting for heat up 100%%...\n");
	//	snprintf(message, 16, "v:10000:0");
	//	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	//	sleep(5);
	//	sunspec_read(ss);
	//	grid = SFI(ss->meter->W, ss->meter->W_SF);
	//	int max_power = round100(grid - offset_start);
	// TODO cmdline parameter
	int max_power = 2000;
	int onepercent = max_power / 100;

	// average offset power at start
	printf("calculating offset start");
	for (int i = 0; i < 10; i++) {
		sunspec_read(ss);
		grid = SFI(ss->meter->W, ss->meter->W_SF);
		printf(" %d", grid);
		offset_start += grid;
		sleep(1);
	}
	offset_start = offset_start / 10 + (offset_start % 10 < 5 ? 0 : 1);
	printf(" --> average %d\n", offset_start);
	sleep(5);

	// do a full drive over SSR characteristic load curve from cold to hot and capture power
	printf("starting measurement with maximum power %d watt 1%%=%d watt\n", max_power, onepercent);
	for (int i = 0; i < 1000; i++) {
		voltage = i * 10;
		snprintf(message, 16, "v:%d:%d", voltage, 0);
		sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
		int ms = 200 < i && i < 800 ? 1000 : 100; // slower between 2 and 8 volt
		msleep(ms);
		sunspec_read(ss);
		measure[i] = SFI(ss->meter->W, ss->meter->W_SF) - offset_start;
		printf("%5d %5d\n", voltage, measure[i]);
	}

	// switch off
	snprintf(message, 16, "v:0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// average offset power at end
	printf("calculating offset end");
	for (int i = 0; i < 10; i++) {
		sunspec_read(ss);
		grid = SFI(ss->meter->W, ss->meter->W_SF);
		printf(" %d", grid);
		offset_end += grid;
		sleep(1);
	}
	offset_end = offset_end / 10 + (offset_end % 10 < 5 ? 0 : 1);
	printf(" --> average %d\n", offset_end);
	sleep(1);

	// build raster table
	raster[0] = 0;
	raster[100] = 10000;
	for (int i = 1; i < 100; i++) {

		// calculate next target power for table index (percent)
		target = onepercent * i;

		// find closest power to target power
		int min_diff = max_power;
		for (int j = 0; j < 1000; j++) {
			int diff = abs(measure[j] - target);
			if (diff < min_diff) {
				min_diff = diff;
				closest = j;
			}
		}

		// find all closest voltages that match target power +/- 5 watt
		int sum = 0, count = 0;
		printf("target power %04d closest %04d range +/-5 watt around closest: ", target, measure[closest]);
		for (int j = 0; j < 1000; j++)
			if (measure[closest] - 5 < measure[j] && measure[j] < measure[closest] + 5) {
				printf(" %d:%d", measure[j], j * 10);
				sum += j * 10;
				count++;
			}

		// average of all closest voltages
		if (count) {
			int z = (sum * 10) / count;
			raster[i] = z / 10 + (z % 10 < 5 ? 0 : 1);
		}
		printf(" --> %dW %dmV\n", target, raster[i]);
	}

	// validate - values in measure table should grow, not shrink
	for (int i = 1; i < 1000; i++)
		if (measure[i - 1] > measure[i]) {
			int v_x = i * 10;
			int m_x = measure[i - 1];
			int v_y = (i - 1) * 10;
			int m_y = measure[i];
			printf("!!! WARNING !!! measuring tainted with parasitic power at voltage %d:%d > %d:%d\n", v_x, m_x, v_y, m_y);
		}
	if (offset_start != offset_end)
		printf("!!! WARNING !!! measuring tainted with parasitic power between start %d and end %d \n", offset_start, offset_end);

	// dump table
	printf("phase angle voltage table 0..100%% in %d watt steps:\n\n", onepercent);
	printf("%d, ", raster[0]);
	for (int i = 1; i <= 100; i++) {
		printf("%d, ", raster[i]);
		if (i % 10 == 0)
			printf("\\\n   ");
	}

	// validate
	printf("\nwaiting 60s for cool down\n");
	sleep(60);
	for (int i = 0; i <= 100; i++) {
		snprintf(message, 16, "v:%d:%d", raster[i], 0);
		sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
		msleep(2000);
		sunspec_read(ss);
		grid = SFI(ss->meter->W, ss->meter->W_SF) - offset_end;
		int expected = onepercent * i;
		float error = grid ? 100.0 - expected * 100.0 / (float) grid : 0;
		printf("%3d%% %5dmV expected %4dW actual %4dW error %.2f\n", i, raster[i], expected, grid, error);
	}

	// switch off
	snprintf(message, 16, "v:0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));

	// cleanup
	close(sock);
	sunspec_stop(ss);
	return 0;
}

// sample grid values from meter
static int grid() {
	pstate_t pp, *p = &pp;
	sunspec_t *ss = sunspec_init("fronius10", 200);
	sunspec_read(ss);
	ss->common = 0;

	while (1) {
		msleep(666);
		sunspec_read(ss);

		p->grid = SFI(ss->meter->W, ss->meter->W_SF);
		p->p1 = SFI(ss->meter->WphA, ss->meter->W_SF);
		p->p2 = SFI(ss->meter->WphB, ss->meter->W_SF);
		p->p3 = SFI(ss->meter->WphC, ss->meter->W_SF);
		p->v1 = SFI(ss->meter->PhVphA, ss->meter->V_SF);
		p->v2 = SFI(ss->meter->PhVphB, ss->meter->V_SF);
		p->v3 = SFI(ss->meter->PhVphC, ss->meter->V_SF);
		p->f = ss->meter->Hz; // without scaling factor

		printf("%5d W  |  %4d W  %4d W  %4d W  |  %d V  %d V  %d V  |  %5.2f Hz\n", p->grid, p->p1, p->p2, p->p3, p->v1, p->v2, p->v3, FLOAT100(p->f));
	}
	return 0;
}

// set charge(-) / discharge(+) limits or reset when 0
static int battery(char *arg) {
	sunspec_t *ss = sunspec_init("fronius10", 1);
	sunspec_read(ss);

	int wh = atoi(arg);
	if (wh > 0)
		return sunspec_storage_limit_discharge(ss, wh);
	if (wh < 0)
		return sunspec_storage_limit_charge(ss, wh * -1);
	return sunspec_storage_limit_reset(ss);
}

// set minimum SoC
static int storage_min(char *arg) {
	sunspec_t *ss = sunspec_init("fronius10", 1);
	sunspec_read(ss);

	int min = atoi(arg);
	return sunspec_storage_minimum_soc(ss, min);
}

static int init() {
	inverter1 = sunspec_init_poll("fronius10", 1, &update_inverter1);
	inverter2 = sunspec_init_poll("fronius7", 2, &update_inverter2);
	meter = sunspec_init_poll("fronius10", 200, &update_meter);

	// use the same lock as both run against same IP address
	meter->lock = inverter1->lock;

	// stop if Fronius10 is not available
	if (!inverter1)
		return xerr("No connection to Fronius10");

	// do not continue before we have SoC value from Fronius10
	int retry = 100;
	while (--retry) {
		msleep(100);
		if (inverter1->storage != 0 && inverter1->storage->ChaState != 0)
			break;
	}
	if (!retry)
		return xerr("No SoC from Fronius10");
	xdebug("SOLAR Fronius10 ready for main loop after retry=%d", retry);

	return 0;
}

static void stop() {
	sunspec_stop(inverter1);
	sunspec_stop(inverter2);
	sunspec_stop(meter);
}

static int test() {
	return 0;
}

int solar_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	int c;
	while ((c = getopt(argc, argv, "b:c:o:s:gt")) != -1) {
		// printf("getopt %c\n", c);
		switch (c) {
		case 'b':
			// -X: limit charge, +X: limit discharge, 0: no limits
			return battery(optarg);
		case 'c':
			// execute as: stdbuf -i0 -o0 -e0 ./solar -c boiler1 > boiler1.txt
			return calibrate(optarg);
		case 'o':
			return solar_override(optarg);
		case 's':
			return storage_min(optarg);
		case 'g':
			return grid();
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

MCP_REGISTER(solar, 10, &init, &stop, NULL);
