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
#include "mqtt.h"
#include "mcp.h"

#define AKKU_CHARGE_MAX			(inverter1 && inverter1->nameplate ? SFI(inverter1->nameplate->MaxChaRte, inverter1->nameplate->MaxChaRte_SF) / 2 : 0)
#define AKKU_DISCHARGE_MAX		(inverter1 && inverter1->nameplate ? SFI(inverter1->nameplate->MaxDisChaRte, inverter1->nameplate->MaxDisChaRte_SF) / 2 : 0)
#define AKKU_CAPACITY			(inverter1 && inverter1->nameplate ? SFI(inverter1->nameplate->WHRtg, inverter1->nameplate->WHRtg_SF) : 0)

#define MIN_SOC					(inverter1 && inverter1->storage ? SFI(inverter1->storage->MinRsvPct, inverter1->storage->MinRsvPct_SF) * 10 : 0)
#define STORCTL					(inverter1 && inverter1->storage ? inverter1->storage->StorCtl_Mod : 0)
#define INWRTE					(inverter1 && inverter1->storage ? inverter1->storage->InWRte : 0)
#define OUTWRTE					(inverter1 && inverter1->storage ? inverter1->storage->OutWRte : 0)

#define PX(x, y)				(x == 1 ? y.p1 : (x == 2 ? y.p2 : y.p3))
#define SAMPLE(x)				x.p1  = SFI(ss->meter->WphA, ss->meter->W_SF); x.p2  = SFI(ss->meter->WphB, ss->meter->W_SF); x.p3  = SFI(ss->meter->WphC, ss->meter->W_SF);
#define SAMPLE_ADD(x)			x.p1 += SFI(ss->meter->WphA, ss->meter->W_SF); x.p2 += SFI(ss->meter->WphB, ss->meter->W_SF); x.p3 += SFI(ss->meter->WphC, ss->meter->W_SF);
#define SUBTRACT(x, y)			x.p1 -= y.p1; x.p2 -= y.p2; x.p3 -= y.p3;
#define PRINTI(i, x)			printf("%5d %4d W  %4d W  %4d W\n", i, x.p1, x.p2, x.p3);
#define PRINTS(s, x)			printf("%s %4d W  %4d W  %4d W\n", s, x.p1, x.p2, x.p3);

// sunspec devices
static sunspec_t *inverter1 = 0, *inverter2 = 0, *meter = 0;

static int control = 1;

int akku_get_min_soc() {
	return MIN_SOC;
}

void akku_set_min_soc(int min) {
	sunspec_storage_minimum_soc(inverter1, min);
}

void akku_state(device_t *akku) {
	xdebug("SOLAR akku storctl=%d inwrte=%d outwrte=%d", STORCTL, INWRTE, OUTWRTE);

	switch (STORCTL) {
	case STORAGE_LIMIT_NONE:
		akku->state = Auto;
		akku->total = params->akku_cmax;
		break;

	case STORAGE_LIMIT_BOTH:
		if (INWRTE == 0 && OUTWRTE == 0) {
			akku->state = Standby;
			akku->total = 0;
		} else if (INWRTE == 0) {
			akku->state = Discharge;
			akku->total = OUTWRTE;
		} else if (OUTWRTE == 0) {
			akku->state = Charge;
			akku->total = INWRTE;
		} else {
			akku->state = Auto;
			akku->total = INWRTE > OUTWRTE ? INWRTE : OUTWRTE;
		}
		break;

	case STORAGE_LIMIT_CHARGE:
		if (INWRTE == 0) {
			akku->state = Discharge;
			akku->total = params->akku_dmax;
		} else {
			akku->state = Charge;
			akku->total = INWRTE;
		}
		break;

	case STORAGE_LIMIT_DISCHARGE:
		if (OUTWRTE == 0) {
			akku->state = Charge;
			akku->total = params->akku_cmax;
		} else {
			akku->state = Discharge;
			akku->total = OUTWRTE;
		}
		break;

	default:
		akku->state = Disabled;
	}
}

int akku_standby(device_t *akku) {
	akku->state = Standby;
	akku->total = 0;

	xdebug("SOLAR set akku STANDBY");
	return sunspec_storage_limit_both(inverter1, 0, 0);
}

int akku_charge(device_t *akku, int limit) {
	akku->state = Charge;

	if (limit) {
		akku->total = limit;
		xdebug("SOLAR set akku CHARGE limit %d", limit);
		return sunspec_storage_limit_both(inverter1, limit, 0);
	} else {
		akku->total = params->akku_cmax;
		xdebug("SOLAR set akku CHARGE");
		return sunspec_storage_limit_discharge(inverter1, 0);
	}
}

int akku_discharge(device_t *akku, int limit) {
	akku->state = Discharge;

	if (limit) {
		akku->total = limit;
		xdebug("SOLAR set akku DISCHARGE limit %d", limit);
		return sunspec_storage_limit_both(inverter1, 0, limit);
	} else {
		akku->total = params->akku_dmax;
		xdebug("SOLAR set akku DISCHARGE");
		return sunspec_storage_limit_charge(inverter1, 0);
	}
}

void inverter_status(device_t *inv1, device_t *inv2) {
	inv1->state = inv2->state = 0;
	if (inverter1 && inverter1->inverter)
		inv1->state = inverter1->inverter->St;
	if (inverter2 && inverter2->inverter)
		inv2->state = inverter2->inverter->St;
}

// inverter1 is Fronius Symo GEN24 10.0 with connected BYD Akku
static void update_inverter1(sunspec_t *ss) {
	pthread_mutex_lock(&collector_lock);

	switch (ss->inverter->St) {
	case I_STATUS_OFF:
		pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = pstate->akku = 0;
		break;

	case I_STATUS_SLEEPING:
		pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = pstate->akku = 0;
		ss->sleep = SLEEP_TIME_SLEEPING; // let the inverter sleep
		break;

	case I_STATUS_STARTING:
		// TODO prüfen ob Werte stimmen
		// pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = pstate->akku = 0;
		break;

	case I_STATUS_MPPT:
		pstate->ac1 = SFI(ss->inverter->W, ss->inverter->W_SF);
		pstate->dc1 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);

		pstate->mppt1 = SFI(ss->mppt->m1_DCW, ss->mppt->DCW_SF);
		pstate->mppt2 = SFI(ss->mppt->m2_DCW, ss->mppt->DCW_SF);

		CM_NOW->mppt1 = SFUI(ss->mppt->m1_DCWH, ss->mppt->DCWH_SF);
		CM_NOW->mppt2 = SFUI(ss->mppt->m2_DCWH, ss->mppt->DCWH_SF);

		// update NULL counter if empty
		if (CM_NULL->mppt1 == 0)
			CM_NULL->mppt1 = CM_NOW->mppt1;
		if (CM_NULL->mppt2 == 0)
			CM_NULL->mppt2 = CM_NOW->mppt2;

		// pstate->akku = pstate->dc1 - (pstate->mppt1 + pstate->mppt2); // akku power is DC power minus PV
		int mppt3 = SFI(ss->mppt->m3_DCW, ss->mppt->DCW_SF);
		int mppt4 = SFI(ss->mppt->m4_DCW, ss->mppt->DCW_SF);
		// xlog("SOLAR %s m3_DCW=%d m4_DCW=%d", ss->name, mppt3, mppt4);
		pstate->akku = mppt3 > 0 ? mppt3 * -1 : mppt4;
		gstate->soc = SFF(ss->storage->ChaState, ss->storage->ChaState_SF) * 10; // store x10 scaled

		// pstate->f = ss->inverter->Hz - 5000; // store only the diff
		// pstate->v1 = SFI(ss->inverter->PhVphA, ss->inverter->V_SF);
		// pstate->v2 = SFI(ss->inverter->PhVphB, ss->inverter->V_SF);
		// pstate->v3 = SFI(ss->inverter->PhVphC, ss->inverter->V_SF);

		ss->sleep = 0;
		break;

	case I_STATUS_FAULT:
		uint16_t active_state_code;
		sunspec_read_reg(ss, 214, &active_state_code);
		xdebug("SOLAR %s inverter St=%d Evt1=%d Evt2=%d F_Active_State_Code=%d", ss->name, ss->inverter->St, ss->inverter->Evt1, ss->inverter->Evt2, active_state_code);
		// cross check - this is normal when no pv is produced
		if (pstate->mppt3 < NOISE)
			ss->sleep = SLEEP_TIME_SLEEPING;
		break;

	default:
		xdebug("SOLAR %s inverter St=%d W=%d DCW=%d ", ss->name, ss->inverter->St, ss->inverter->W, ss->inverter->DCW);
		// ss->sleep = SLEEP_TIME_FAULT;
	}

	pthread_mutex_unlock(&collector_lock);
}

// inverter2 is Fronius Symo 7.0-3-M
static void update_inverter2(sunspec_t *ss) {
	pthread_mutex_lock(&collector_lock);

	switch (ss->inverter->St) {
	case I_STATUS_OFF:
		pstate->ac2 = pstate->dc2 = pstate->mppt3 = pstate->mppt4;
		break;

	case I_STATUS_SLEEPING:
		pstate->ac2 = pstate->dc2 = pstate->mppt3 = pstate->mppt4 = 0;
		ss->sleep = SLEEP_TIME_SLEEPING; // let the inverter sleep
		break;

	case I_STATUS_STARTING:
		// TODO prüfen ob Werte stimmen
		// pstate->ac2 = pstate->dc2 = pstate->mppt3 = pstate->mppt4 = 0;
		break;

	case I_STATUS_MPPT:
		pstate->ac2 = SFI(ss->inverter->W, ss->inverter->W_SF);
		pstate->dc2 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);

		pstate->mppt3 = SFI(ss->mppt->m1_DCW, ss->mppt->DCW_SF);
		pstate->mppt4 = SFI(ss->mppt->m2_DCW, ss->mppt->DCW_SF);

		CM_NOW->mppt3 = SFUI(ss->mppt->m1_DCWH, ss->mppt->DCWH_SF);
		CM_NOW->mppt4 = SFUI(ss->mppt->m2_DCWH, ss->mppt->DCWH_SF);

		// update NULL counter if empty
		if (CM_NULL->mppt3 == 0)
			CM_NULL->mppt3 = CM_NOW->mppt3;
		if (CM_NULL->mppt4 == 0)
			CM_NULL->mppt4 = CM_NOW->mppt4;

		ss->sleep = 0;
		break;

	case I_STATUS_FAULT:
		uint16_t active_state_code;
		sunspec_read_reg(ss, 214, &active_state_code);
		xdebug("SOLAR %s inverter St=%d Evt1=%d Evt2=%d F_Active_State_Code=%d", ss->name, ss->inverter->St, ss->inverter->Evt1, ss->inverter->Evt2, active_state_code);
		// cross check - this is normal when no pv is produced
		if (pstate->mppt1 < NOISE)
			ss->sleep = SLEEP_TIME_SLEEPING;
		break;

	default:
		xdebug("SOLAR %s inverter St=%d W=%d DCW=%d ", ss->name, ss->inverter->St, ss->inverter->W, ss->inverter->DCW);
		// ss->sleep = SLEEP_TIME_FAULT;
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
	pstate->v1 = SFI(ss->meter->PhVphA, ss->meter->V_SF);
	pstate->v2 = SFI(ss->meter->PhVphB, ss->meter->V_SF);
	pstate->v3 = SFI(ss->meter->PhVphC, ss->meter->V_SF);
	pstate->f = ss->meter->Hz - 5000; // store only the diff

	CM_NOW->consumed = SFUI(ss->meter->TotWhImp, ss->meter->TotWh_SF);
	CM_NOW->produced = SFUI(ss->meter->TotWhExp, ss->meter->TotWh_SF);

	// update NULL counter if empty
	if (CM_NULL->produced == 0)
		CM_NULL->produced = CM_NOW->produced;
	if (CM_NULL->consumed == 0)
		CM_NULL->consumed = CM_NOW->consumed;

	pthread_mutex_unlock(&collector_lock);
}

static void drive(int sock, struct sockaddr *sa, sunspec_t *ss, pstate_t measure[], pstate_t poffset, int voffset, int delay) {
	char message[16];
	int voltage, index;

	for (int i = 0; i < 100; i++) {
		voltage = (voffset + i) * 10;
		snprintf(message, 16, "v:%d:%d", voltage, 0);
		sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
		index = (voffset + i) - (3000 / delay - 1) > 0 ? 3000 / delay - 1 : 0;
		msleep(delay);
		sunspec_read(ss);
		SAMPLE(measure[index])
		SUBTRACT(measure[index], poffset)
		PRINTI(voltage, measure[index])
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
	int closest, target;
	pstate_t offset_start, offset_end, measure[1000];
	int raster[101];

	// create a sunspec handle for meter and remove models not needed
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
	printf("calculating offset start\n");
	ZERO(offset_start);
	for (int i = 0; i < 10; i++) {
		sunspec_read(ss);
		SAMPLE_ADD(offset_start)
		PRINTI(i, offset_start)
		sleep(1);
	}
	offset_start.p1 = offset_start.p1 / 10 + (offset_start.p1 % 10 < 5 ? 0 : 1);
	offset_start.p2 = offset_start.p2 / 10 + (offset_start.p2 % 10 < 5 ? 0 : 1);
	offset_start.p3 = offset_start.p3 / 10 + (offset_start.p3 % 10 < 5 ? 0 : 1);
	PRINTS("average offset_start --> ", offset_start);
	sleep(5);

	// do a full drive over SSR characteristic load curve from cold to hot and capture power
	printf("starting measurement with maximum power %d watt 1%%=%d watt\n", max_power, onepercent);
	drive(sock, sa, ss, measure, offset_start, 0, 100);
	drive(sock, sa, ss, measure, offset_start, 100, 100);
	drive(sock, sa, ss, measure, offset_start, 200, 3000);
	drive(sock, sa, ss, measure, offset_start, 300, 500);
	drive(sock, sa, ss, measure, offset_start, 400, 500);
	drive(sock, sa, ss, measure, offset_start, 500, 500);
	drive(sock, sa, ss, measure, offset_start, 600, 500);
	drive(sock, sa, ss, measure, offset_start, 700, 500);
	drive(sock, sa, ss, measure, offset_start, 800, 100);
	drive(sock, sa, ss, measure, offset_start, 900, 100);

	// switch off
	snprintf(message, 16, "v:0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// average offset power at end
	printf("calculating offset end\n");
	ZERO(offset_end);
	for (int i = 0; i < 10; i++) {
		sunspec_read(ss);
		SAMPLE_ADD(offset_end)
		PRINTI(i, offset_end)
		sleep(1);
	}
	offset_end.p1 = offset_end.p1 / 10 + (offset_end.p1 % 10 < 5 ? 0 : 1);
	offset_end.p2 = offset_end.p2 / 10 + (offset_end.p2 % 10 < 5 ? 0 : 1);
	offset_end.p3 = offset_end.p3 / 10 + (offset_end.p3 % 10 < 5 ? 0 : 1);
	PRINTS("average offset_end --> ", offset_end);

	// find phase
	int p = 0;
	if (measure[666].p1 > max_power / 2)
		p = 1;
	if (measure[666].p2 > max_power / 2)
		p = 2;
	if (measure[666].p3 > max_power / 2)
		p = 3;
	if (!p)
		printf("unable to detect phase\n");
	else
		printf("detected phase %d\n", p);

	// build raster table
	raster[0] = 0;
	raster[100] = 10000;
	for (int i = 1; i < 100; i++) {

		// calculate next target power for table index (percent)
		target = onepercent * i;

		// find closest power to target power
		int min_diff = max_power;
		for (int j = 0; j < 1000; j++) {
			int diff = abs(PX(p, measure[j]) - target);
			if (diff < min_diff) {
				min_diff = diff;
				closest = j;
			}
		}
		int mc = PX(p, measure[closest]);

		// find all closest voltages that match target power +/- 5 watt
		int sum = 0, count = 0;
		printf("target power %04d closest %04d range +/-5 watt around closest: ", target, mc);
		for (int j = 0; j < 1000; j++) {
			int mj = PX(p, measure[j]);
			if (mc - 5 < mj && mj < mc + 5) {
				printf(" %d:%d", mj, j * 10);
				sum += j * 10;
				count++;
			}
		}

		// average of all closest voltages
		if (count) {
			int z = (sum * 10) / count;
			raster[i] = z / 10 + (z % 10 < 5 ? 0 : 1);
		}
		printf(" --> %dW %dmV\n", target, raster[i]);
	}

	int poffset_start = PX(p, offset_start);
	int poffset_end = PX(p, offset_end);
	if (poffset_start != poffset_end)
		printf("!!! WARNING !!! measuring tainted with parasitic power between start %d and end %d \n", poffset_start, poffset_end);

	// dump table
	printf("phase angle voltage table 0..100%% in %d watt steps:\n\n", onepercent);
	printf("%d, ", raster[0]);
	for (int i = 1; i <= 100; i++) {
		printf("%d, ", raster[i]);
		if (i % 10 == 0)
			printf("\\\n   ");
	}

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

static int latency() {
	int count;
	time_t ts;
	pstate_t x, y;

#define DEVICE	0x9D01FD
#define RELAY	3

	char topic[LINEBUF];
	snprintf(topic, LINEBUF, "cmnd/%6X/POWER%d", DEVICE, RELAY);

	// create a sunspec handle for meter and remove models not needed
	sunspec_t *ss = sunspec_init("fronius10", 200);
	sunspec_read(ss);
	ss->common = 0;
	ss->storage = 0;

	// middle of second
	ts = time(NULL);
	while (ts == time(NULL))
		msleep(111);
	msleep(500);

	// switch on
	count = 0;
	SAMPLE(x)
	PRINTI(count, x)
//  add to Makefile: tasmota.o mqtt.o frozen.o
//	publish_oneshot(topic, "ON", 0);
	while (1) {
		msleep(100);
		count++;
		sunspec_read(ss);
		SAMPLE(y)
		PRINTI(count, y)
		int dp1 = abs(x.p1 - y.p1);
		int dp2 = abs(x.p2 - y.p2);
		int dp3 = abs(x.p3 - y.p3);
		if (dp1 > 100 || dp2 > 100 || dp3 > 100)
			break;
	}
	printf("detected ON meter response after %dms\n", count * 100);

	// middle of second
	sleep(5);
	ts = time(NULL);
	while (ts == time(NULL))
		msleep(111);
	msleep(500);

	// switch off
	count = 0;
	SAMPLE(x)
	PRINTI(count, x)
//  add to Makefile: tasmota.o mqtt.o frozen.o
//	publish_oneshot(topic, "OFF", 0);
	while (1) {
		msleep(100);
		count++;
		sunspec_read(ss);
		SAMPLE(y)
		PRINTI(count, y)
		int dp1 = abs(x.p1 - y.p1);
		int dp2 = abs(x.p2 - y.p2);
		int dp3 = abs(x.p3 - y.p3);
		if (dp1 > 100 || dp2 > 100 || dp3 > 100)
			break;
	}
	printf("detected OFF meter response after %dms\n", count * 100);

	return 0;
}
static int init() {
	inverter1 = sunspec_init_poll("fronius10", 1, &update_inverter1);
	inverter2 = sunspec_init_poll("fronius7", 2, &update_inverter2);
	meter = sunspec_init_poll("fronius10", 200, &update_meter);

	// use the same lock as both run against same IP address
	meter->lock = inverter1->lock;

	// allow storage control
	inverter1->control = control;

	// stop if Fronius10 is not available
	if (!inverter1)
		return xerr("No connection to inverter1");

	// do not continue before we have SoC value from Fronius10
	int retry = 100;
	while (--retry) {
		msleep(100);
		if (inverter1->storage != 0 && inverter1->storage->ChaState != 0)
			break;
	}
	if (!retry)
		return xerr("No SoC from %d", inverter1->name);

	params->akku_capacity = AKKU_CAPACITY;
	params->akku_cmax = AKKU_CHARGE_MAX;
	params->akku_dmax = AKKU_DISCHARGE_MAX;
	xlog("SOLAR modbus %s ready retry=%d Akku capacity=%d cmax=%d dmax=%d", inverter1->name, retry, params->akku_capacity, params->akku_cmax, params->akku_dmax);

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
	int c;
	while ((c = getopt(argc, argv, "ab:c:s:glt")) != -1) {
		// printf("getopt %c\n", c);
		switch (c) {
		case 'a':
			return latency();
		case 'b':
			// -X: limit charge, +X: limit discharge, 0: no limits
			return battery(optarg);
		case 'c':
			// execute as: stdbuf -i0 -o0 -e0 ./solar -c boiler1 > boiler1.txt
			return calibrate(optarg);
		case 's':
			return storage_min(optarg);
		case 'g':
			return grid();
		case 'l':
			control = 0; // disable storage control in loop mode
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

MCP_REGISTER(solar, 10, &init, &stop, NULL);
