#include "tasmota-devices.h"
#include "solar-common.h"
#include "sunspec.h"
#include "utils.h"

#define AKKU_BURNOUT			1
#define BASELOAD				(WINTER ? 300 : 200)
#define MINIMUM					(BASELOAD / 2)

#ifndef SOLAR_MAIN
#define TEMP_IN					sensors->htu21_temp
#define TEMP_OUT				sensors->sht31_temp
#endif

#define CALIBRATION

// devices
static device_t a1 = { .name = "akku", .total = 0, .ramp = &ramp_akku, .adj = 0 }, *AKKU = &a1;
static device_t b1 = { .name = "boiler1", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t b2 = { .name = "boiler2", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t b3 = { .name = "boiler3", .total = 2000, .ramp = &ramp_boiler, .adj = 1 }, *B3 = &b3;
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
#define SUNSPEC_INVERTER1
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
		counter->mppt1 = SFUI(ss->mppt->m1_DCWH, ss->mppt->DCWH_SF);
		counter->mppt2 = SFUI(ss->mppt->m2_DCWH, ss->mppt->DCWH_SF);
		ss->sleep = 0;
		ss->active = 1;
		// update counter hour 0 when empty
		if (COUNTER_0->mppt1 == 0)
			COUNTER_0->mppt1 = counter->mppt1;
		if (COUNTER_0->mppt2 == 0)
			COUNTER_0->mppt2 = counter->mppt2;
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
#define SUNSPEC_INVERTER2
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
		counter->mppt3 = SFUI(ss->mppt->m1_DCWH, ss->mppt->DCWH_SF);
		counter->mppt4 = SFUI(ss->mppt->m2_DCWH, ss->mppt->DCWH_SF);
		ss->sleep = 0;
		ss->active = 1;
		// update counter hour 0 when empty
		if (COUNTER_0->mppt3 == 0)
			COUNTER_0->mppt3 = counter->mppt3;
		if (COUNTER_0->mppt4 == 0)
			COUNTER_0->mppt4 = counter->mppt4;
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
#define SUNSPEC_METER
static sunspec_t *meter = 0;
static void update_meter(sunspec_t *ss) {
	pthread_mutex_lock(&pstate_lock);

	counter->produced = SFUI(ss->meter->TotWhExp, ss->meter->TotWh_SF);
	counter->consumed = SFUI(ss->meter->TotWhImp, ss->meter->TotWh_SF);
	pstate->grid = SFI(ss->meter->W, ss->meter->W_SF);
	pstate->p1 = SFI(ss->meter->WphA, ss->meter->W_SF);
	pstate->p2 = SFI(ss->meter->WphB, ss->meter->W_SF);
	pstate->p3 = SFI(ss->meter->WphC, ss->meter->W_SF);
//	pstate->v1 = SFI(ss->meter->PhVphA, ss->meter->V_SF);
//	pstate->v2 = SFI(ss->meter->PhVphB, ss->meter->V_SF);
//	pstate->v3 = SFI(ss->meter->PhVphC, ss->meter->V_SF);
//	pstate->f = ss->meter->Hz - 5000; // store only the diff
	// update counter hour 0 when empty
	if (COUNTER_0->produced == 0)
		COUNTER_0->produced = counter->produced;
	if (COUNTER_0->consumed == 0)
		COUNTER_0->consumed = counter->consumed;

	pthread_mutex_unlock(&pstate_lock);
}

static int solar_init() {
	inverter1 = sunspec_init_poll("fronius10", 1, &update_inverter1);
	inverter2 = sunspec_init_poll("fronius7", 2, &update_inverter2);
	meter = sunspec_init_poll("fronius10", 200, &update_meter);

	// stop if Fronius10 is not available
	if (!inverter1)
		return xerr("No connection to Fronius10");

	return 0;
}

static void solar_stop() {
	sunspec_stop(inverter1);
	sunspec_stop(inverter2);
	sunspec_stop(meter);
}

// sample grid values from meter
static int grid() {
#ifdef SUNSPEC_METER
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
#endif
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
