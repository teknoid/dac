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

#define PX(x, y)				(x == 1 ? y.l1p : (x == 2 ? y.l2p : y.l3p))
#define SAMPLE(x)				x.l1p  = SFI(ss->meter->WphA, ss->meter->W_SF); x.l2p  = SFI(ss->meter->WphB, ss->meter->W_SF); x.l3p  = SFI(ss->meter->WphC, ss->meter->W_SF);
#define SAMPLE_ADD(x)			x.l1p += SFI(ss->meter->WphA, ss->meter->W_SF); x.l2p += SFI(ss->meter->WphB, ss->meter->W_SF); x.l3p += SFI(ss->meter->WphC, ss->meter->W_SF);
#define SUBTRACT(x, y)			x.l1p -= y.l1p; x.l2p -= y.l2p; x.l3p -= y.l3p;
#define PRINTI(i, x)			printf("%5d %4d W  %4d W  %4d W\n", i, x.l1p, x.l2p, x.l3p);
#define PRINTS(s, x)			printf("%s %4d W  %4d W  %4d W\n", s, x.l1p, x.l2p, x.l3p);

// sunspec devices
static sunspec_t *inverter1 = 0, *inverter2 = 0, *meter = 0;

static int control = 1;

void inverter_off() {
	sunspec_controls_conn(inverter1, 0);
}

void inverter_on() {
	sunspec_controls_conn(inverter1, 1);
}

int akku_get_min_soc() {
	return MIN_SOC;
}

void akku_set_min_soc(int min) {
	sunspec_storage_minimum_soc(inverter1, min);
}

void akku_state(device_t *akku) {
	xdebug("SOLAR akku storctl=%d inwrte=%d outwrte=%d", STORCTL, INWRTE, OUTWRTE);

	switch (STORCTL) {
	case (FLAG_LIMIT_CHARGE | FLAG_LIMIT_DISCHARGE):
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

	case FLAG_LIMIT_CHARGE:
		if (INWRTE == 0) {
			akku->state = Discharge;
			akku->total = params->akku_dmax;
		} else {
			akku->state = Charge;
			akku->total = INWRTE;
		}
		break;

	case FLAG_LIMIT_DISCHARGE:
		if (OUTWRTE == 0) {
			akku->state = Charge;
			akku->total = params->akku_cmax;
		} else {
			akku->state = Discharge;
			akku->total = OUTWRTE;
		}
		break;

	default:
		akku->state = Auto;
		akku->total = params->akku_cmax;
	}
}

int akku_charge(device_t *akku) {
	akku->state = Charge;
	sunspec_storage_charge(inverter1, akku->climit);
	return 0;
}

int akku_discharge(device_t *akku) {
	akku->state = Discharge;
	sunspec_storage_discharge(inverter1, akku->dlimit);
	return 0;
}

int akku_standby(device_t *akku) {
	akku->state = Standby;
	akku->total = 0;
	return sunspec_storage_standby(inverter1);
}

// inverter1 is Fronius Symo GEN24 10.0 with connected BYD Akku
static void update_inverter1(sunspec_t *ss) {
	pthread_mutex_lock(&collector_lock);
	ss->sleep = 0;

	// mppt voltage is always available
	pstate->mppt1v = SFI(ss->mppt->m1_DCV, ss->mppt->DCV_SF);
	pstate->mppt2v = SFI(ss->mppt->m2_DCV, ss->mppt->DCV_SF);

	inv1->state = ss->inverter->St;
	switch (ss->inverter->St) {
	case I_STATUS_OFF:
		pstate->ac1 = pstate->dc1 = pstate->mppt1p = pstate->mppt2p = pstate->akku = 0;
		break;

	case I_STATUS_STANDBY:
	case I_STATUS_SLEEPING:
		pstate->ac1 = pstate->dc1 = pstate->mppt1p = pstate->mppt2p = pstate->akku = 0;
		ss->sleep = SLEEP_TIME_SLEEPING; // let the inverter sleep
		break;

	case I_STATUS_STARTING:
		// TODO prüfen ob Werte stimmen
		// pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = pstate->akku = 0;
		break;

	case I_STATUS_MPPT:
		pstate->ac1 = SFI(ss->inverter->W, ss->inverter->W_SF);
		pstate->dc1 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);

		pstate->mppt1p = SFI(ss->mppt->m1_DCW, ss->mppt->DCW_SF);
		pstate->mppt2p = SFI(ss->mppt->m2_DCW, ss->mppt->DCW_SF);

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
		break;

	case I_STATUS_FAULT:
		uint16_t active_state_code = 0;
		sunspec_read_reg(ss, 214, &active_state_code);
		xdebug("SOLAR %s inverter St=%d Evt1=%d Evt2=%d F_Active_State_Code=%d", ss->name, ss->inverter->St, ss->inverter->Evt1, ss->inverter->Evt2, active_state_code);
		// cross check - this is normal when no pv is produced
		if (pstate->mppt3p < NOISE10)
			ss->sleep = SLEEP_TIME_SLEEPING;
		break;

	default:
		xdebug("SOLAR %s inverter St=%d W=%d DCW=%d ", ss->name, ss->inverter->St, ss->inverter->W, ss->inverter->DCW);
		pstate->ac1 = pstate->dc1 = pstate->mppt1p = pstate->mppt2p = pstate->mppt1v = pstate->mppt2v = pstate->akku = 0;
	}

	pthread_mutex_unlock(&collector_lock);
}

// inverter2 is Fronius Symo 7.0-3-M
static void update_inverter2(sunspec_t *ss) {
	pthread_mutex_lock(&collector_lock);
	ss->sleep = 0;

	// mppt voltage is always available
	pstate->mppt3v = SFI(ss->mppt->m1_DCV, ss->mppt->DCV_SF);
	pstate->mppt4v = SFI(ss->mppt->m2_DCV, ss->mppt->DCV_SF);

	inv2->state = ss->inverter->St;
	switch (ss->inverter->St) {
	case I_STATUS_OFF:
		pstate->ac2 = pstate->dc2 = pstate->mppt3p = pstate->mppt4p;
		break;

	case I_STATUS_STANDBY:
	case I_STATUS_SLEEPING:
		pstate->ac2 = pstate->dc2 = pstate->mppt3p = pstate->mppt4p = 0;
		ss->sleep = SLEEP_TIME_SLEEPING; // let the inverter sleep
		break;

	case I_STATUS_STARTING:
		// TODO prüfen ob Werte stimmen
		// pstate->ac2 = pstate->dc2 = pstate->mppt3 = pstate->mppt4 = 0;
		break;

	case I_STATUS_MPPT:
		pstate->ac2 = SFI(ss->inverter->W, ss->inverter->W_SF);
		pstate->dc2 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);

		pstate->mppt3p = SFI(ss->mppt->m1_DCW, ss->mppt->DCW_SF);
		pstate->mppt4p = SFI(ss->mppt->m2_DCW, ss->mppt->DCW_SF);

		CM_NOW->mppt3 = SFUI(ss->mppt->m1_DCWH, ss->mppt->DCWH_SF);
		CM_NOW->mppt4 = SFUI(ss->mppt->m2_DCWH, ss->mppt->DCWH_SF);

		// update NULL counter if empty
		if (CM_NULL->mppt3 == 0)
			CM_NULL->mppt3 = CM_NOW->mppt3;
		if (CM_NULL->mppt4 == 0)
			CM_NULL->mppt4 = CM_NOW->mppt4;
		break;

	case I_STATUS_FAULT:
		// uint16_t active_state_code = 0;
		// sunspec_read_reg(ss, 214, &active_state_code);
		// xdebug("SOLAR %s inverter St=%d Evt1=%d Evt2=%d F_Active_State_Code=%d", ss->name, ss->inverter->St, ss->inverter->Evt1, ss->inverter->Evt2, active_state_code);
		// cross check - this is normal when no pv is produced
		if (pstate->mppt1p < NOISE10)
			ss->sleep = SLEEP_TIME_SLEEPING;
		break;

	default:
		xdebug("SOLAR %s inverter St=%d W=%d DCW=%d ", ss->name, ss->inverter->St, ss->inverter->W, ss->inverter->DCW);
		pstate->ac2 = pstate->dc2 = pstate->mppt3p = pstate->mppt4p = pstate->mppt3v = pstate->mppt4v = 0;
	}

	// fix disconnected MPPT4
	pstate->mppt4p = pstate->mppt4v = 0;

	pthread_mutex_unlock(&collector_lock);
}

// meter is Fronius Smart Meter TS 65A-3
static void update_meter(sunspec_t *ss) {
	pthread_mutex_lock(&collector_lock);

	pstate->grid = SFI(ss->meter->W, ss->meter->W_SF);
	pstate->l1p = SFI(ss->meter->WphA, ss->meter->W_SF);
	pstate->l2p = SFI(ss->meter->WphB, ss->meter->W_SF);
	pstate->l3p = SFI(ss->meter->WphC, ss->meter->W_SF);
	pstate->l1v = ss->meter->PhVphA; // keep decimals
	pstate->l2v = ss->meter->PhVphB; // keep decimals
	pstate->l3v = ss->meter->PhVphC; // keep decimals
	pstate->f = ss->meter->Hz - 5000; // store difference

	CM_NOW->consumed = SFUI(ss->meter->TotWhImp, ss->meter->TotWh_SF);
	CM_NOW->produced = SFUI(ss->meter->TotWhExp, ss->meter->TotWh_SF);

	// update NULL counter if empty
	if (CM_NULL->produced == 0)
		CM_NULL->produced = CM_NOW->produced;
	if (CM_NULL->consumed == 0)
		CM_NULL->consumed = CM_NOW->consumed;

	pthread_mutex_unlock(&collector_lock);
}

static int evaluate(char *name) {
	char filename[32];
	int closest, target;
	pstate_t measure[1000];
	int raster[101];

	// TODO cmdline parameter
	int max_power = 2000;
	int onepercent = max_power / 100;

	// read
	snprintf(filename, 32, "/tmp/%s.bin", name);
	load_blob(filename, measure, sizeof(measure));

	// find phase
	pstate_t sum;
	ZERO(sum);
	for (int i = 1; i < 1000; i++)
		iadd(&sum, &measure[i], PSTATE_SIZE);
	int p = 0;
	if (sum.l1p > sum.l2p && sum.l1p > sum.l3p)
		p = 1;
	if (sum.l2p > sum.l1p && sum.l1p > sum.l3p)
		p = 2;
	if (sum.l3p > sum.l1p && sum.l3p > sum.l2p)
		p = 3;
	if (!p)
		printf("unable to detect phase\n");
	else
		printf("detected phase %d\n", p);

	// round power values
	for (int i = 1; i < 1000; i++) {
		measure[i].l1p = round10(measure[i].l1p);
		measure[i].l2p = round10(measure[i].l2p);
		measure[i].l3p = round10(measure[i].l3p);
	}

	// build raster table
	raster[0] = 0;
	raster[100] = 10000;
	for (int i = 1; i < 100; i++) {

		// calculate next target power for table index (percent)
		target = onepercent * i;

		// find closest power to target power
		for (int j = 0; j < 1000; j++)
			if (measure[i].l1p == target)
				printf("%d W match %d mV\n", target, j);

		printf(" --> %dW %dmV\n", target, raster[i]);
	}

	// dump table
	printf("phase angle voltage table 0..100%% in %d watt steps:\n\n", onepercent);
	printf("%d, ", raster[0]);
	for (int i = 1; i <= 100; i++) {
		printf("%d, ", raster[i]);
		if (i % 10 == 0)
			printf("\\\n   ");
	}

	return 0;
}

static void drive(int sock, struct sockaddr *sa, sunspec_t *ss, pstate_t measure[], pstate_t poffset, int voffset, int mod, int delay) {
	char message[16];
	for (int i = 0; i < 100; i++) {
		if (i % mod != 0)
			continue;
		int idx = voffset + i;
		int voltage = idx * 10;
		snprintf(message, 16, "v:%d:%d", voltage, 0);
		// sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
		msleep(delay);
		sunspec_read(ss);
		SAMPLE(measure[idx])
		SUBTRACT(measure[idx], poffset)
		PRINTI(voltage, measure[idx])
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
	char message[16], filename[32];
	pstate_t offset_start, offset_end, measure[1000];

	// disable akku discharge
//	printf("disable akku discharge\n");
//	sunspec_t *ssbyd = sunspec_init("fronius10", 1);
//	sunspec_read(ssbyd);
//	sunspec_storage_limit_discharge(ssbyd, 0);
//	sleep(5);

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
	offset_start.l1p = offset_start.l1p / 10 + (offset_start.l1p % 10 < 5 ? 0 : 1);
	offset_start.l2p = offset_start.l2p / 10 + (offset_start.l2p % 10 < 5 ? 0 : 1);
	offset_start.l3p = offset_start.l3p / 10 + (offset_start.l3p % 10 < 5 ? 0 : 1);
	PRINTS("average offset_start --> ", offset_start);
	sleep(5);

	// do a full drive over SSR characteristic load curve from cold to hot and capture power
	printf("starting measurement with maximum power %d watt 1%%=%d watt\n", max_power, onepercent);
	ZERO(measure);
	drive(sock, sa, ss, measure, offset_start, 200, 1, 1000);
	drive(sock, sa, ss, measure, offset_start, 300, 2, 1000);
	drive(sock, sa, ss, measure, offset_start, 400, 3, 1000);
	drive(sock, sa, ss, measure, offset_start, 500, 5, 1000);
	drive(sock, sa, ss, measure, offset_start, 600, 7, 1000);
	drive(sock, sa, ss, measure, offset_start, 700, 10, 1000);

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
	offset_end.l1p = offset_end.l1p / 10 + (offset_end.l1p % 10 < 5 ? 0 : 1);
	offset_end.l2p = offset_end.l2p / 10 + (offset_end.l2p % 10 < 5 ? 0 : 1);
	offset_end.l3p = offset_end.l3p / 10 + (offset_end.l3p % 10 < 5 ? 0 : 1);
	PRINTS("average offset_start --> ", offset_start);
	PRINTS("average offset_end   --> ", offset_end);

	// write
	snprintf(filename, 32, "/tmp/%s.csv", name);
	store_table_csv(measure, PSTATE_SIZE, 1000, PSTATE_HEADER, filename);
	snprintf(filename, 32, "/tmp/%s.bin", name);
	store_blob(filename, measure, sizeof(measure));

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
		p->l1p = SFI(ss->meter->WphA, ss->meter->W_SF);
		p->l2p = SFI(ss->meter->WphB, ss->meter->W_SF);
		p->l3p = SFI(ss->meter->WphC, ss->meter->W_SF);
		p->l1v = SFI(ss->meter->PhVphA, ss->meter->V_SF);
		p->l2v = SFI(ss->meter->PhVphB, ss->meter->V_SF);
		p->l3v = SFI(ss->meter->PhVphC, ss->meter->V_SF);
		p->f = ss->meter->Hz; // without scaling factor

		printf("%5d W  |  %4d W  %4d W  %4d W  |  %d V  %d V  %d V  |  %5.2f Hz\n", p->grid, p->l1p, p->l2p, p->l3p, p->l1v, p->l2v, p->l3v, FLOAT100(p->f));
	}
	return 0;
}

// set charge(-) / discharge(+) limits or reset when 0
static int battery(char *arg) {
	// TODO MQTT

	sunspec_t *ss = sunspec_init("fronius10", 1);
	sunspec_read(ss);

	int wh = atoi(arg);
	if (wh > 0)
		return sunspec_storage_discharge(ss, wh);
	if (wh < 0)
		return sunspec_storage_charge(ss, wh * -1);
	return sunspec_storage_auto(ss);
}

// set minimum SoC
static int storage_min(char *arg) {
	sunspec_t *ss = sunspec_init("fronius10", 1);
	sunspec_read(ss);

	int min = atoi(arg);
	return sunspec_storage_minimum_soc(ss, min);
}

// set minimum SoC
static int inverter_on_off(char *arg) {
	sunspec_t *ss = sunspec_init("fronius10", 1);
	sunspec_read(ss);

	int on = atoi(arg);
	return sunspec_controls_conn(inverter1, on);
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
		int dl1p = abs(x.l1p - y.l1p);
		int dl2p = abs(x.l2p - y.l2p);
		int dl3p = abs(x.l3p - y.l3p);
		if (dl1p > 100 || dl2p > 100 || dl3p > 100)
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
		int dl1p = abs(x.l1p - y.l1p);
		int dl2p = abs(x.l2p - y.l2p);
		int dl3p = abs(x.l3p - y.l3p);
		if (dl1p > 100 || dl2p > 100 || dl3p > 100)
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
		return xerr("SOLAR modbus no connection to inverter1");

	// do not continue before we have SoC value from Fronius10
	int retry = 100;
	while (--retry) {
		msleep(100);
		if (inverter1->storage != 0 && inverter1->storage->ChaState != 0)
			break;
	}
	if (!retry)
		return xerr("SOLAR modbus no SoC from %s", inverter1->name);

	params->akku_capacity = AKKU_CAPACITY;
	params->akku_cmax = AKKU_CHARGE_MAX;
	params->akku_dmax = AKKU_DISCHARGE_MAX;
	xlog("SOLAR modbus %s ready retry=%d Akku capacity=%d cmax=%d dmax=%d", inverter1->name, retry, params->akku_capacity, params->akku_cmax, params->akku_dmax);

	// minimum SoC: 5%
	sunspec_storage_minimum_soc(inverter1, 5);

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
	while ((c = getopt(argc, argv, "ab:c:e:gi:ls:t")) != -1) {
		// printf("getopt %c\n", c);
		switch (c) {
		case 'a':
			return latency();
		case 'b':
			// -X: limit charge, +X: limit discharge, 0: no limits
			return battery(optarg);
		case 'c':
			// ./solar -c boiler1
			return calibrate(optarg);
		case 'e':
			// ./solar -e boiler1
			return evaluate(optarg);
		case 'g':
			return grid();
		case 'i':
			return inverter_on_off(optarg);
		case 'l':
			control = 0; // disable storage control in loop mode
			return mcp_main(argc, argv);
		case 's':
			return storage_min(optarg);
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
