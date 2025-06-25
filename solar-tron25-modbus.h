#include <sys/socket.h>
#include <arpa/inet.h>

#include "tasmota-devices.h"
#include "solar-common.h"
#include "sunspec.h"
#include "utils.h"

#define AKKU_BURNOUT			1
#define BASELOAD				(WINTER ? 300 : 200)
#define MINIMUM					(BASELOAD / 2)

#define TEMP_IN					sensors->htu21_temp
#define TEMP_OUT				sensors->sht31_temp

#define COUNTER_METER

// devices
static device_t a1 = { .name = "akku", .total = 0, .ramp = &ramp_akku, .adj = 0 }, *AKKU = &a1;
static device_t b1 = { .name = "boiler1", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t b2 = { .name = "boiler2", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t b3 = { .name = "boiler3", .total = 2000, .ramp = &ramp_boiler, .adj = 1, .from = 11, .to = 15, .min = 100 };
static device_t h1 = { .name = "küche", .total = 500, .ramp = &ramp_heater, .adj = 0, .id = SWITCHBOX, .r = 1 };
static device_t h2 = { .name = "wozi", .total = 500, .ramp = &ramp_heater, .adj = 0, .id = SWITCHBOX, .r = 2 };
static device_t h3 = { .name = "schlaf", .total = 500, .ramp = &ramp_heater, .adj = 0, .id = PLUG5, .r = 0 };
static device_t h4 = { .name = "tisch", .total = 200, .ramp = &ramp_heater, .adj = 0, .id = SWITCHBOX, .r = 3, };

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

// inverter1 is  Fronius Symo GEN24 10.0 with connected BYD Akku
static sunspec_t *inverter1 = 0;
#define MIN_SOC					(inverter1 ? SFI(inverter1->storage->MinRsvPct, inverter1->storage->MinRsvPct_SF) * 10 : 0)
#define AKKU_CHARGE_MAX			(inverter1 ? SFI(inverter1->nameplate->MaxChaRte, inverter1->nameplate->MaxChaRte_SF) / 2 : 0)
#define AKKU_DISCHARGE_MAX		(inverter1 ? SFI(inverter1->nameplate->MaxDisChaRte, inverter1->nameplate->MaxDisChaRte_SF) / 2 : 0)
#define AKKU_CAPACITY			(inverter1 ? SFI(inverter1->nameplate->WHRtg, inverter1->nameplate->WHRtg_SF) : 0)
static void update_inverter1(sunspec_t *ss) {
	pthread_mutex_lock(&pstate_lock);

	pstate->f = ss->inverter->Hz - 5000; // store only the diff
	pstate->v1 = SFI(ss->inverter->PhVphA, ss->inverter->V_SF);
	pstate->v2 = SFI(ss->inverter->PhVphB, ss->inverter->V_SF);
	pstate->v3 = SFI(ss->inverter->PhVphC, ss->inverter->V_SF);
	pstate->soc = SFF(ss->storage->ChaState, ss->storage->ChaState_SF) * 10;

	switch (ss->inverter->St) {
	case I_STATUS_STARTING:
		pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = 0;
		break;

	case I_STATUS_MPPT:
		// only take over values in MPPT state
		pstate->ac1 = SFI(ss->inverter->W, ss->inverter->W_SF);
		pstate->dc1 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);
		pstate->mppt1 = SFI(ss->mppt->m1_DCW, ss->mppt->DCW_SF);
		pstate->mppt2 = SFI(ss->mppt->m2_DCW, ss->mppt->DCW_SF);
		// TODO find sunspec register
		pstate->akku = pstate->dc1 - (pstate->mppt1 + pstate->mppt2); // akku power is DC power minus PV

		// dissipation
		// int dissipation = pstate->dc1 - pstate->ac1 - pstate->akku;
		// xlog("SOLAR Inverter1 dissipation=%d", dissipation);

		CM_NOW->mppt1 = SFUI(ss->mppt->m1_DCWH, ss->mppt->DCWH_SF);
		CM_NOW->mppt2 = SFUI(ss->mppt->m2_DCWH, ss->mppt->DCWH_SF);

		ss->sleep = 0;
		ss->active = 1;

		// update NULL counter if empty
		if (CM_NULL->mppt1 == 0)
			CM_NULL->mppt1 = CM_NOW->mppt1;
		if (CM_NULL->mppt2 == 0)
			CM_NULL->mppt2 = CM_NOW->mppt2;
		break;

	case I_STATUS_SLEEPING:
		// let the inverter sleep
		pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = 0;
		ss->sleep = SLEEP_TIME_SLEEPING;
		ss->active = 0;
		break;

	default:
		xdebug("SOLAR %s inverter St %d W %d DCW %d ", ss->name, ss->inverter->St, ss->inverter->W, ss->inverter->DCW);
		ss->sleep = SLEEP_TIME_FAULT;
		ss->active = 0;
	}

	pthread_mutex_unlock(&pstate_lock);
}

// inverter2 is Fronius Symo 7.0-3-M
static sunspec_t *inverter2 = 0;
static void update_inverter2(sunspec_t *ss) {
	pthread_mutex_lock(&pstate_lock);

	switch (ss->inverter->St) {
	case I_STATUS_STARTING:
		pstate->ac2 = pstate->dc2 = pstate->mppt3 = pstate->mppt4 = 0;
		break;

	case I_STATUS_MPPT:
		// only take over values in MPPT state
		pstate->ac2 = SFI(ss->inverter->W, ss->inverter->W_SF);
		pstate->dc2 = SFI(ss->inverter->DCW, ss->inverter->DCW_SF);
		pstate->mppt3 = SFI(ss->mppt->m1_DCW, ss->mppt->DCW_SF);
		pstate->mppt4 = SFI(ss->mppt->m2_DCW, ss->mppt->DCW_SF);

		// dissipation
		// int dissipation = pstate->dc1 - pstate->ac1;
		// xlog("SOLAR Inverter2 dissipation=%d", dissipation);

		CM_NOW->mppt3 = SFUI(ss->mppt->m1_DCWH, ss->mppt->DCWH_SF);
		CM_NOW->mppt4 = SFUI(ss->mppt->m2_DCWH, ss->mppt->DCWH_SF);

		ss->sleep = 0;
		ss->active = 1;

		// update NULL counter if empty
		if (CM_NULL->mppt3 == 0)
			CM_NULL->mppt3 = CM_NOW->mppt3;
		if (CM_NULL->mppt4 == 0)
			CM_NULL->mppt4 = CM_NOW->mppt4;
		break;

	case I_STATUS_SLEEPING:
		// let the inverter sleep
		pstate->ac2 = pstate->dc2 = pstate->mppt3 = pstate->mppt4 = 0;
		ss->sleep = SLEEP_TIME_SLEEPING;
		ss->active = 0;
		break;

	default:
		// xdebug("SOLAR %s inverter St %d W %d DCW %d ", ss->name, ss->inverter->St, ss->inverter->W, ss->inverter->DCW);
		ss->sleep = SLEEP_TIME_FAULT;
		ss->active = 0;
	}

	pthread_mutex_unlock(&pstate_lock);
}

// meter is Fronius Smart Meter TS 65A-3
static sunspec_t *meter = 0;
static void update_meter(sunspec_t *ss) {
	pthread_mutex_lock(&pstate_lock);

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

	pthread_mutex_unlock(&pstate_lock);
}

static int solar_init() {
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

static void solar_stop() {
	sunspec_stop(inverter1);
	sunspec_stop(inverter2);
	sunspec_stop(meter);
}

static void inverter_status(char *line) {
	char value[16];

	strcat(line, "   F");
	if (inverter1 && inverter1->inverter) {
		snprintf(value, 16, ":%d", inverter1->inverter->St);
		strcat(line, value);
	}
	if (inverter2 && inverter2->inverter) {
		snprintf(value, 16, ":%d", inverter2->inverter->St);
		strcat(line, value);
	}
}

static void inverter_valid() {
	if (inverter1 && !inverter1->active) {
		xdebug("SOLAR Inverter1 is not active!");
		pstate->flags &= ~FLAG_VALID;
	}
	if (inverter2 && !inverter2->active) {
		xdebug("SOLAR Inverter2 is not active!");
	}
}

static void akku_strategy() {
	// storage strategy: standard 5%, winter and tomorrow not much PV expected 10%
	int min = WINTER && gstate->tomorrow < AKKU_CAPACITY && gstate->soc > 111 ? 10 : 5;
	sunspec_storage_minimum_soc(inverter1, min);
}

static int akku_standby() {
	AKKU->state = Standby;
	AKKU->power = 0;
#ifndef SOLAR_MAIN
	if (!sunspec_storage_limit_both(inverter1, 0, 0))
		xdebug("SOLAR set akku STANDBY");
#endif
	return 0; // continue loop
}

static int akku_charge() {
	AKKU->state = Charge;
	AKKU->power = 1;
#ifndef SOLAR_MAIN
	if (!sunspec_storage_limit_discharge(inverter1, 0)) {
		xdebug("SOLAR set akku CHARGE");
		return WAIT_AKKU_CHARGE; // loop done
	}
#endif
	return 0; // continue loop
}

static int akku_discharge() {
	AKKU->state = Discharge;
	AKKU->power = 0;
	int limit = WINTER && (gstate->survive < 0 || gstate->tomorrow < AKKU_CAPACITY);
	if (limit) {
#ifndef SOLAR_MAIN
		if (!sunspec_storage_limit_both(inverter1, 0, BASELOAD)) {
			xdebug("SOLAR set akku DISCHARGE limit BASELOAD");
			return WAIT_RESPONSE; // loop done
		}
#endif
	} else {
#ifndef SOLAR_MAIN
		if (!sunspec_storage_limit_charge(inverter1, 0)) {
			xdebug("SOLAR set akku DISCHARGE");
			return WAIT_RESPONSE; // loop done
		}
#endif
	}
	return 0; // continue loop
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

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

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
