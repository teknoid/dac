#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include "solar-common.h"
#include "sensors.h"
#include "sunspec.h"
#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

#define COUNTER_METER

#define AKKU_BURNOUT			1

#define GNUPLOT					"/usr/bin/gnuplot -p /home/hje/workspace-cpp/dac/misc/solar.gp"

#define POWERFLOW_TEMPLATE		"{\"common\":{\"datestamp\":\"01.01.2025\",\"timestamp\":\"00:00:00\"},\"inverters\":[{\"BatMode\":1,\"CID\":0,\"DT\":0,\"E_Total\":1,\"ID\":1,\"P\":1,\"SOC\":%f}],\"site\":{\"BackupMode\":false,\"BatteryStandby\":false,\"E_Day\":null,\"E_Total\":1,\"E_Year\":null,\"MLoc\":0,\"Mode\":\"bidirectional\",\"P_Akku\":%d,\"P_Grid\":%d,\"P_Load\":%d,\"P_PV\":%d,\"rel_Autonomy\":100.0,\"rel_SelfConsumption\":100.0},\"version\":\"13\"}"

// hexdump -v -e '6 "%10d ""\n"' /var/lib/mcp/solar-counter.bin
#define COUNTER_H_FILE			"solar-counter-hours.bin"
#define COUNTER_FILE			"solar-counter.bin"

// hexdump -v -e '19 "%6d ""\n"' /var/lib/mcp/solar-gstate.bin
#define GSTATE_H_FILE			"solar-gstate-hours.bin"
#define GSTATE_M_FILE			"solar-gstate-minutes.bin"
#define GSTATE_FILE				"solar-gstate.bin"

// hexdump -v -e '28 "%6d ""\n"' /var/lib/mcp/solar-pstate*.bin
#define PSTATE_H_FILE			"solar-pstate-hours.bin"
#define PSTATE_M_FILE			"solar-pstate-minutes.bin"
#define PSTATE_S_FILE			"solar-pstate-seconds.bin"
#define PSTATE_FILE				"solar-pstate.bin"

// CSV files for gnuplot
#define GSTATE_TODAY_CSV		"gstate-today.csv"
#define GSTATE_WEEK_CSV			"gstate-week.csv"
#define GSTATE_M_CSV			"gstate-minutes.csv"
#define PSTATE_M_CSV			"pstate-minutes.csv"
#define LOADS_CSV				"loads.csv"

// JSON files for webui
#define PSTATE_JSON				"pstate.json"
#define GSTATE_JSON				"gstate.json"
#define DSTATE_JSON				"dstate.json"
#define POWERFLOW_JSON			"powerflow.json"

// counter history
#define COUNTER_HOUR_NOW		(&counter_history[24 * now->tm_wday + now->tm_hour])

// gstate access pointers
#define GSTATE_MIN_NOW			(&gstate_minutes[now->tm_min])
#define GSTATE_MIN_LAST			(&gstate_minutes[now->tm_min > 0 ? now->tm_min - 1 : 59])
#define GSTATE_HOUR_NOW			(&gstate_history[24 * now->tm_wday + now->tm_hour])
#define GSTATE_HOUR_LAST		(&gstate_history[24 * now->tm_wday + now->tm_hour - (now->tm_wday == 0 && now->tm_hour ==  0 ?  24 * 7 - 1 : 1)])
#define GSTATE_HOUR_NEXT		(&gstate_history[24 * now->tm_wday + now->tm_hour + (now->tm_wday == 6 && now->tm_hour == 23 ? -24 * 7 + 1 : 1)])
#define GSTATE_TODAY			(&gstate_history[24 * now->tm_wday])
#define GSTATE_YDAY				(&gstate_history[24 * (now->tm_wday > 0 ? now->tm_wday - 1 : 6)])
#define GSTATE_HOUR(h)			(&gstate_history[24 * now->tm_wday + (h)])
#define GSTATE_DAY_HOUR(d, h)	(&gstate_history[24 * (d) + (h)])

// pstate access pointers
#define PSTATE_SEC_NOW			(&pstate_seconds[now->tm_sec])
#define PSTATE_SEC_NEXT			(&pstate_seconds[now->tm_sec < 59 ? now->tm_sec + 1 : 0])
#define PSTATE_SEC_LAST1		(&pstate_seconds[now->tm_sec > 0 ? now->tm_sec - 1 : 59])
#define PSTATE_SEC_LAST2		(&pstate_seconds[now->tm_sec > 1 ? now->tm_sec - 2 : (now->tm_sec - 2 + 60)])
#define PSTATE_SEC_LAST3		(&pstate_seconds[now->tm_sec > 2 ? now->tm_sec - 3 : (now->tm_sec - 3 + 60)])
#define PSTATE_MIN_NOW			(&pstate_minutes[now->tm_min])
#define PSTATE_MIN_LAST1		(&pstate_minutes[now->tm_min > 0 ? now->tm_min - 1 : 59])
#define PSTATE_MIN_LAST2		(&pstate_minutes[now->tm_min > 1 ? now->tm_min - 2 : (now->tm_min - 2 + 60)])
#define PSTATE_MIN_LAST3		(&pstate_minutes[now->tm_min > 2 ? now->tm_min - 3 : (now->tm_min - 3 + 60)])
#define PSTATE_HOUR_NOW			(&pstate_hours[now->tm_hour])
//#define PSTATE_HOUR_LAST1		(&pstate_hours[now->tm_hour > 0 ? now->tm_hour - 1 : 23])
#define PSTATE_HOUR(h)			(&pstate_hours[h])

// average loads over 24/7
static int loads[24];

static struct tm now_tm, *now = &now_tm;

// local counter/pstate/gstate memory
static counter_t counter_history[HISTORY_SIZE];
static gstate_t gstate_history[HISTORY_SIZE], gstate_minutes[60], gstate_current;
static pstate_t pstate_seconds[60], pstate_minutes[60], pstate_hours[24], pstate_current;

// global counter/pstate/gstate pointer
counter_t counter[10];
gstate_t *gstate = &gstate_current;
pstate_t *pstate = &pstate_current;

// mutex for updating / calculating pstate and counter
pthread_mutex_t collector_lock;

static void load_state() {
	load_blob(STATE SLASH COUNTER_FILE, counter, sizeof(counter));
	load_blob(STATE SLASH COUNTER_H_FILE, counter_history, sizeof(counter_history));

	load_blob(STATE SLASH GSTATE_H_FILE, gstate_history, sizeof(gstate_history));
	load_blob(STATE SLASH GSTATE_M_FILE, gstate_minutes, sizeof(gstate_minutes));
	load_blob(STATE SLASH GSTATE_FILE, gstate, sizeof(gstate_current));

	load_blob(STATE SLASH PSTATE_H_FILE, pstate_hours, sizeof(pstate_hours));
	load_blob(STATE SLASH PSTATE_M_FILE, pstate_minutes, sizeof(pstate_minutes));
	load_blob(STATE SLASH PSTATE_S_FILE, pstate_seconds, sizeof(pstate_seconds));
	load_blob(STATE SLASH PSTATE_FILE, pstate, sizeof(pstate_current));
}

static void store_state() {
	store_blob(STATE SLASH COUNTER_FILE, counter, sizeof(counter));
	store_blob(STATE SLASH COUNTER_H_FILE, counter_history, sizeof(counter_history));

	store_blob(STATE SLASH GSTATE_H_FILE, gstate_history, sizeof(gstate_history));
	store_blob(STATE SLASH GSTATE_M_FILE, gstate_minutes, sizeof(gstate_minutes));
	store_blob(STATE SLASH GSTATE_FILE, gstate, sizeof(gstate_current));

	store_blob(STATE SLASH PSTATE_H_FILE, pstate_hours, sizeof(pstate_hours));
	store_blob(STATE SLASH PSTATE_M_FILE, pstate_minutes, sizeof(pstate_minutes));
	store_blob(STATE SLASH PSTATE_S_FILE, pstate_seconds, sizeof(pstate_seconds));
	store_blob(STATE SLASH PSTATE_FILE, pstate, sizeof(pstate_current));
}

static void create_pstate_json() {
	store_struct_json((int*) pstate, PSTATE_SIZE, PSTATE_HEADER, RUN SLASH PSTATE_JSON);
}

static void create_gstate_json() {
	store_struct_json((int*) gstate, GSTATE_SIZE, GSTATE_HEADER, RUN SLASH GSTATE_JSON);
}

// feed Fronius powerflow web application
static void create_powerflow_json() {
	FILE *fp = fopen(RUN SLASH POWERFLOW_JSON, "wt");
	if (fp == NULL)
		return;

	fprintf(fp, POWERFLOW_TEMPLATE, FLOAT10(pstate->soc), pstate->akku, pstate->grid, pstate->load, pstate->pv);
	fflush(fp);
	fclose(fp);
}

static void collect_loads() {
	char line[LINEBUF], value[10];

	ZERO(loads);
	for (int h = 0; h < 24; h++) {
		for (int d = 0; d < 7; d++) {
			int load = GSTATE_DAY_HOUR(d, h)->load * -1;
			if (load == 0)
				load = BASELOAD;
			if (load < NOISE) {
				xdebug("SOLAR suspicious collect_loads day=%d hour=%d load=%d --> using BASELOAD", d, h, load);
				load = BASELOAD;
			}
			loads[h] += load;
		}
		loads[h] /= 7;
	}

	strcpy(line, "SOLAR average 24/7 loads:");
	for (int h = 0; h < 24; h++) {
		snprintf(value, 10, " %d", loads[h]);
		strcat(line, value);
	}
	xdebug(line);

	store_array_csv(loads, 24, 1, "  load", RUN SLASH LOADS_CSV);
}

static void print_gstate() {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "GSTATE ");
	xlogl_bits16(line, NULL, gstate->flags);
	xlogl_int(line, "Today", gstate->today);
	xlogl_int(line, "Tomo", gstate->tomorrow);
//	xlogl_int(line, "SoD", gstate->sod);
//	xlogl_int(line, "EoD", gstate->eod);
//	xlogl_int(line, "Akku", gstate->akku);
	xlogl_float(line, "SoC", FLOAT10(gstate->soc));
	xlogl_float(line, "TTL", FLOAT60(gstate->ttl));
	xlogl_float(line, "Ti", sensors->tin);
	xlogl_float(line, "To", sensors->tout);
	xlogl_int_b(line, "∑PV", gstate->pv);
	xlogl_int_noise(line, NOISE, 0, "↑Grid", gstate->produced);
	xlogl_int_noise(line, NOISE, 1, "↓Grid", gstate->consumed);
	xlogl_percent10(line, "Succ", gstate->success);
	xlogl_percent10(line, "Surv", gstate->survive);
	xlogl_end(line, strlen(line), 0);
}

static void print_pstate() {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "PSTATE ");
	xlogl_bits16(line, NULL, pstate->flags);
	if (!PSTATE_OFFLINE) {
		xlogl_int_b(line, "PV10", pstate->mppt1 + pstate->mppt2);
		xlogl_int_b(line, "PV7", pstate->mppt3 + pstate->mppt4);
	}
	xlogl_int_noise(line, NOISE, 1, "Grid", pstate->grid);
	xlogl_int_noise(line, NOISE, 1, "Akku", pstate->akku);
	xlogl_int(line, "Load", pstate->load);
	xlogl_int(line, "Inv", pstate->inv);
	xlogl_end(line, strlen(line), 0);
}

static void calculate_counter() {
	pthread_mutex_lock(&collector_lock);

	// meter counter daily - calculate delta to NULL entry
	CM_DAY->consumed = CM_NOW->consumed && CM_NULL->consumed ? CM_NOW->consumed - CM_NULL->consumed : 0;
	CM_DAY->produced = CM_NOW->produced && CM_NULL->produced ? CM_NOW->produced - CM_NULL->produced : 0;
	CM_DAY->mppt1 = CM_NOW->mppt1 && CM_NULL->mppt1 ? CM_NOW->mppt1 - CM_NULL->mppt1 : 0;
	CM_DAY->mppt2 = CM_NOW->mppt2 && CM_NULL->mppt2 ? CM_NOW->mppt2 - CM_NULL->mppt2 : 0;
	CM_DAY->mppt3 = CM_NOW->mppt3 && CM_NULL->mppt3 ? CM_NOW->mppt3 - CM_NULL->mppt3 : 0;
	CM_DAY->mppt4 = CM_NOW->mppt4 && CM_NULL->mppt4 ? CM_NOW->mppt4 - CM_NULL->mppt4 : 0;

	// self counter daily - convert Watt-secons to Watt-hours
	CS_DAY->consumed = CS_NOW->consumed / 3600;
	CS_DAY->produced = CS_NOW->produced / 3600;
	CS_DAY->mppt1 = CS_NOW->mppt1 / 3600;
	CS_DAY->mppt2 = CS_NOW->mppt2 / 3600;
	CS_DAY->mppt3 = CS_NOW->mppt3 / 3600;
	CS_DAY->mppt4 = CS_NOW->mppt4 / 3600;

	if (HOURLY) {
		// meter counter for last hour
		CM_HOUR->consumed = CM_NOW->consumed && CM_LAST->consumed ? CM_NOW->consumed - CM_LAST->consumed : 0;
		CM_HOUR->produced = CM_NOW->produced && CM_LAST->produced ? CM_NOW->produced - CM_LAST->produced : 0;
		CM_HOUR->mppt1 = CM_NOW->mppt1 && CM_LAST->mppt1 ? CM_NOW->mppt1 - CM_LAST->mppt1 : 0;
		CM_HOUR->mppt2 = CM_NOW->mppt2 && CM_LAST->mppt2 ? CM_NOW->mppt2 - CM_LAST->mppt2 : 0;
		CM_HOUR->mppt3 = CM_NOW->mppt3 && CM_LAST->mppt3 ? CM_NOW->mppt3 - CM_LAST->mppt3 : 0;
		CM_HOUR->mppt4 = CM_NOW->mppt4 && CM_LAST->mppt4 ? CM_NOW->mppt4 - CM_LAST->mppt4 : 0;

		// self counter for last hour
		CS_HOUR->consumed = (CS_NOW->consumed - CS_LAST->consumed) / 3600;
		CS_HOUR->produced = (CS_NOW->produced - CS_LAST->produced) / 3600;
		CS_HOUR->mppt1 = (CS_NOW->mppt1 - CS_LAST->mppt1) / 3600;
		CS_HOUR->mppt2 = (CS_NOW->mppt2 - CS_LAST->mppt2) / 3600;
		CS_HOUR->mppt3 = (CS_NOW->mppt3 - CS_LAST->mppt3) / 3600;
		CS_HOUR->mppt4 = (CS_NOW->mppt4 - CS_LAST->mppt4) / 3600;

		// compare hourly self and meter counter
		xlog("FRONIUS counter meter cons=%d prod=%d 1=%d 2=%d 3=%d 4=%d", CM_HOUR->consumed, CM_HOUR->produced, CM_HOUR->mppt1, CM_HOUR->mppt2, CM_HOUR->mppt3, CM_HOUR->mppt4);
		xlog("FRONIUS counter self  cons=%d prod=%d 1=%d 2=%d 3=%d 4=%d", CS_HOUR->consumed, CS_HOUR->produced, CS_HOUR->mppt1, CS_HOUR->mppt2, CS_HOUR->mppt3, CS_HOUR->mppt4);

		// copy to history
#ifdef COUNTER_METER
		memcpy(COUNTER_HOUR_NOW, (void*) CM_NOW, sizeof(counter_t));
		mosmix_mppt(now, CM_HOUR->mppt1, CM_HOUR->mppt2, CM_HOUR->mppt3, CM_HOUR->mppt4);
#else
		memcpy(COUNTER_HOUR_NOW, (void*) CS_NOW, sizeof(counter_t));
		mosmix_mppt(now, CS_HOUR->mppt1, CS_HOUR->mppt2, CS_HOUR->mppt3, CS_HOUR->mppt4);
#endif

		// copy to LAST entry
		memcpy(CM_LAST, CM_NOW, sizeof(counter_t));
		memcpy(CS_LAST, CS_NOW, sizeof(counter_t));

		if (DAILY) {
			// compare daily self and meter counter
			xlog("FRONIUS counter meter  cons=%d prod=%d 1=%d 2=%d 3=%d 4=%d", CM_DAY->consumed, CM_DAY->produced, CM_DAY->mppt1, CM_DAY->mppt2, CM_DAY->mppt3, CM_DAY->mppt4);
			xlog("FRONIUS counter self   cons=%d prod=%d 1=%d 2=%d 3=%d 4=%d", CS_DAY->consumed, CS_DAY->produced, CS_DAY->mppt1, CS_DAY->mppt2, CS_DAY->mppt3, CS_DAY->mppt4);

			// reset self counter and copy self/meter counter to NULL entry
			memcpy(CM_NULL, CM_NOW, sizeof(counter_t));
			ZEROP(CS_NOW);
			ZEROP(CS_LAST);
			memcpy(CS_NULL, CS_NOW, sizeof(counter_t));
		}
	}

	pthread_mutex_unlock(&collector_lock);
}

static void calculate_gstate() {
	// clear state flags and values
	gstate->flags = 0;

	// summer / winter mode
	if (SUMMER)
		gstate->flags |= FLAG_SUMMER;

	if (WINTER)
		gstate->flags |= FLAG_WINTER;

	// day total: consumed / produced / pv
#ifdef COUNTER_METER
	gstate->consumed = CM_DAY->consumed;
	gstate->produced = CM_DAY->produced;
	gstate->pv = CM_DAY->mppt1 + CM_DAY->mppt2 + CM_DAY->mppt3 + CM_DAY->mppt4;
#else
	gstate->consumed = CS_DAY->consumed;
	gstate->produced = CS_DAY->produced;
	gstate->pv = CS_DAY->mppt1 + CS_DAY->mppt2 + CS_DAY->mppt3 + CS_DAY->mppt4;
#endif

	// take over pstate values
	gstate->soc = pstate->soc;

	// store average load in gstate history
	gstate->load = PSTATE_HOUR_NOW->load;

	// akku usable energy and estimated time to live based on last hour's average load +5% extra +25 inverter dissipation
	int min = akku_get_min_soc();
	int capa = akku_capacity();
	gstate->akku = gstate->soc > min ? capa * (gstate->soc - min) / 1000 : 0;
	gstate->ttl = gstate->soc > min ? gstate->akku * 60 / (gstate->load + gstate->load / 20 - 25) * -1 : 0;

	// collect mosmix forecasts
	int today, tomorrow, sod, eod;
	mosmix_collect(now, &tomorrow, &today, &sod, &eod);
	gstate->tomorrow = tomorrow;
	gstate->today = today;
	gstate->sod = sod;
	gstate->eod = eod;
	gstate->success = sod > MINIMUM && gstate->pv > NOISE ? gstate->pv * 1000 / sod : 0;
	CUT(gstate->success, 2000);
	xdebug("SOLAR pv=%d sod=%d eod=%d success=%.1f%%", gstate->pv, sod, eod, FLOAT10(gstate->success));

	// survival factor
	int tocharge = gstate->need_survive - gstate->akku;
	CUT_LOW(tocharge, 0);
	int available = gstate->eod - tocharge;
	CUT_LOW(available, 0);
	if (gstate->sod == 0)
		available = 0; // pv not yet started - we only have akku
	gstate->survive = gstate->need_survive ? (available + gstate->akku) * 1000 / gstate->need_survive : 0;
	CUT(gstate->survive, 2000);
	xdebug("SOLAR survive eod=%d tocharge=%d avail=%d akku=%d need=%d --> %.1f%%", gstate->eod, tocharge, available, gstate->akku, gstate->need_survive, FLOAT10(gstate->survive));

	// heating
	gstate->flags |= FLAG_HEATING;
	// no need to heat
	if (sensors->tin > 18.0 && SUMMER)
		gstate->flags &= ~FLAG_HEATING;
	if (sensors->tin > 22.0 && sensors->tout > 15.0 && !SUMMER)
		gstate->flags &= ~FLAG_HEATING;
	if (sensors->tin > 25.0)
		gstate->flags &= ~FLAG_HEATING;
	// force heating
	if ((now->tm_mon == 4 || now->tm_mon == 8) && now->tm_hour >= 16 && sensors->tin < 25.0) // may/sept begin 16 o'clock
		gstate->flags |= FLAG_HEATING;
	else if ((now->tm_mon == 3 || now->tm_mon == 9) && now->tm_hour >= 14 && sensors->tin < 25.0) // apr/oct begin 14 o'clock
		gstate->flags |= FLAG_HEATING;
	else if ((now->tm_mon < 3 || now->tm_mon > 9) && sensors->tin < 28.0) // nov-mar always
		gstate->flags |= FLAG_HEATING;

	// start akku charging
	int soc6 = GSTATE_HOUR(6)->soc;
	int time_window = now->tm_hour >= 9 && now->tm_hour < 15;
	if (WINTER)
		// winter: always at any time
		gstate->flags |= FLAG_CHARGE_AKKU;
	else if (SUMMER) {
		// summer: between 9 and 15 o'clock when below 22%
		if (time_window && soc6 < 222)
			gstate->flags |= FLAG_CHARGE_AKKU;
	} else {
		// autumn/spring: between 9 and 15 o'clock when below 33% or tomorrow not enough pv
		if (time_window && soc6 < 333)
			gstate->flags |= FLAG_CHARGE_AKKU;
		if (time_window && gstate->tomorrow < akku_capacity() * 2)
			gstate->flags |= FLAG_CHARGE_AKKU;
	}

	// copy to history
	memcpy(GSTATE_MIN_NOW, (void*) gstate, sizeof(gstate_t));
	if (HOURLY)
		memcpy(GSTATE_HOUR_NOW, (void*) gstate, sizeof(gstate_t));

	print_gstate();
}

static void calculate_pstate() {
	// lock while calculating
	pthread_mutex_lock(&collector_lock);

	// inverter status
	int inv1, inv2;
	inverter_status(&inv1, &inv2);
	pstate->inv = inv1 * 10 + inv2;

	// clear state flags and values
	pstate->flags = 0;

	// clear delta sum counters every minute
	if (MINLY)
		pstate->sdpv = pstate->sdgrid = pstate->sdload = 0;

	// update self counter before shaping
	if (pstate->grid > 0)
		CS_NOW->consumed += pstate->grid;
	if (pstate->grid < 0)
		CS_NOW->produced += pstate->grid * -1;
	CS_NOW->mppt1 += pstate->mppt1;
	CS_NOW->mppt2 += pstate->mppt2;
	CS_NOW->mppt3 += pstate->mppt3;
	CS_NOW->mppt4 += pstate->mppt4;

	// get history states
	pstate_t *s1 = PSTATE_SEC_LAST1;
	pstate_t *s2 = PSTATE_SEC_LAST2;
	pstate_t *s3 = PSTATE_SEC_LAST3;
	pstate_t *m0 = PSTATE_MIN_NOW;
	pstate_t *m1 = PSTATE_MIN_LAST1;
	pstate_t *m2 = PSTATE_MIN_LAST2;

	// total PV produced by all strings
	if (pstate->mppt1 == 1)
		pstate->mppt1 = 0; // noise
	if (pstate->mppt2 == 1)
		pstate->mppt2 = 0; // noise
	if (pstate->mppt3 == 1)
		pstate->mppt3 = 0; // noise
	if (pstate->mppt4 == 1)
		pstate->mppt4 = 0; // noise
	pstate->pv = pstate->mppt1 + pstate->mppt2 + pstate->mppt3 + pstate->mppt4;
	pstate->dpv = pstate->pv - s1->pv;
	if (abs(pstate->dpv) < NOISE)
		pstate->dpv = 0; // shape dpv
	pstate->sdpv += abs(pstate->dpv);

	// grid, delta grid and sum
	pstate->dgrid = pstate->grid - s1->grid;
	if (abs(pstate->dgrid) < NOISE)
		pstate->dgrid = 0; // shape dgrid
	pstate->sdgrid += abs(pstate->dgrid);

	// load, delta load + sum
	pstate->load = (pstate->ac1 + pstate->ac2 + pstate->grid) * -1;
	pstate->dload = pstate->load - s1->load;
	if (abs(pstate->dload) < NOISE)
		pstate->dload = 0; // shape dload
	pstate->sdload += abs(pstate->dload);

	// check if we have delta ac power anywhere
	if (abs(pstate->grid - s1->grid) > DELTA)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac1 - s1->ac1) > DELTA)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac2 - s1->ac2) > DELTA)
		pstate->flags |= FLAG_DELTA;

	// grid upload in last 3 minutes
	if (pstate->grid < -NOISE) {
		int g2 = m0->grid < -25 && m1->grid < -25 && m2->grid < -25;
		int g1 = m0->grid < -50 && m1->grid < -50;
		int g0 = m0->grid < -75;
		if (g2 || g1 || g0) {
			pstate->flags |= FLAG_GRID_ULOAD;
			xdebug("SOLAR set FLAG_GRID_ULOAD last 3=%d 2=%d 1=%d", m2->grid, m1->grid, m0->grid);
		}
	}

	// grid download in last 3 minutes
	if (pstate->grid > NOISE) {
		int g2 = m0->grid > 25 && m1->grid > 25 && m2->grid > 25;
		int g1 = m0->grid > 50 && m1->grid > 50;
		int g0 = m0->grid > 75;
		if (g2 || g1 || g0) {
			pstate->flags |= FLAG_GRID_DLOAD;
			xdebug("SOLAR set FLAG_GRID_DLOAD last 3=%d 2=%d 1=%d", m2->grid, m1->grid, m0->grid);
		}
	}

	// akku discharge in last 3 minutes
	if (pstate->akku > NOISE) {
		int a2 = m0->akku > 25 && m1->akku > 25 && m2->akku > 25;
		int a1 = m0->akku > 50 && m1->akku > 50;
		int a0 = m0->akku > 75;
		if (a2 || a1 || a0) {
			pstate->flags |= FLAG_AKKU_DCHARGE;
			xdebug("SOLAR set FLAG_AKKU_DCHARGE last 3=%d 2=%d 1=%d", m2->akku, m1->akku, m0->akku);
		}
	}

	// offline mode when average PV is below average load in last 3 minutes
	int o2 = m2->pv < m2->load * -1 + NOISE;
	int o1 = m1->pv < m1->load * -1 + NOISE;
	int o0 = m0->pv < m0->load * -1 + NOISE;
	if (o2 && o1 && o0) {
		// akku burn out between 6 and 9 o'clock if we can re-charge it completely by day
		int burnout_time = now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8;
		int burnout_possible = sensors->tin < 18.0 && pstate->soc > 150;
		if (burnout_time && burnout_possible && AKKU_BURNOUT)
			pstate->flags |= FLAG_BURNOUT; // burnout
		else
			pstate->flags |= FLAG_OFFLINE; // offline

	} else {
		// online

		// emergency shutdown: extreme grid download or last minute big akku discharge / grid download
		int E1 = EMERGENCY, E2 = E1 * 2;
		int e1 = E1 && (m0->akku > E1 || m0->grid > E1);
		int e2 = E2 && pstate->grid > E2 && s1->grid > E2 && s2->grid > E2 && s3->grid > E2;
		if (e1 || e2) {
			pstate->flags |= FLAG_EMERGENCY;
			xlog("SOLAR set FLAG_EMERGENCY akku=%d grid=%d m0->akku=%d m0->grid=%d", pstate->akku, pstate->grid, m0->akku, m0->grid);
		}

		// first set and then clear VALID flag when values suspicious
		pstate->flags |= FLAG_VALID;
		int sum = pstate->grid + pstate->akku + pstate->load + pstate->pv;
		if (abs(sum) > SUSPICIOUS) { // probably inverter power dissipations (?)
			xdebug("SOLAR suspicious values detected: sum=%d", sum);
			pstate->flags &= ~FLAG_VALID;
		}
		if (pstate->load > 0) {
			xdebug("SOLAR positive load detected");
			pstate->flags &= ~FLAG_VALID;
		}
		if (pstate->grid < -NOISE && pstate->akku > NOISE) {
			int waste = abs(pstate->grid) < pstate->akku ? abs(pstate->grid) : pstate->akku;
			xdebug("SOLAR wasting power %d akku -> grid", waste);
			pstate->flags &= ~FLAG_VALID;
		}
		if (pstate->dgrid > BASELOAD * 2) { // e.g. refrigerator starts !!!
			xdebug("SOLAR grid spike detected %d: %d -> %d", pstate->grid - s1->grid, s1->grid, pstate->grid);
			pstate->flags &= ~FLAG_VALID;
		}
		if (inv1 != I_STATUS_MPPT) {
			xdebug("SOLAR Inverter1 state %d != %d", inv1, I_STATUS_MPPT);
			pstate->flags &= ~FLAG_VALID;
		}
		if (inv2 != I_STATUS_MPPT) {
			xdebug("SOLAR Inverter2 state %d != %d ", inv2, I_STATUS_MPPT);
			// pstate->flags &= ~FLAG_VALID;
		}

		// state is stable when we have 3x no grid changes
		if (!pstate->dgrid && !s1->dgrid && !s2->dgrid)
			pstate->flags |= FLAG_STABLE;

		// distortion when current sdpv is too big or aggregated last two sdpv's are too big
		int d0 = pstate->sdpv > m0->pv;
		int d1 = m0->sdpv > m0->pv + m0->pv / 2;
		int d2 = m1->sdpv > m1->pv + m1->pv / 2;
		if (d0 || d1 || d2) {
			pstate->flags |= FLAG_DISTORTION;
			xdebug("SOLAR set FLAG_DISTORTION 0=%d/%d 1=%d/%d 2=%d/%d", pstate->sdpv, pstate->pv, m0->sdpv, m0->pv, m1->sdpv, m1->pv);
		}

		// PV tendency: rising or falling
		int r3 = s1->dpv > 25 && s2->dpv > 25 && s3->dpv > 25;
		int r2 = s1->dpv > 50 && s2->dpv > 50;
		int r1 = s1->dpv > 100;
		if (r3 || r2 || r1) {
			pstate->flags |= FLAG_PV_RISING;
			xdebug("SOLAR set FLAG_PV_RISING 3=%d 2=%d 1=%d", s3->dpv, s2->dpv, s1->dpv);
		}
		int f3 = s1->dpv < -25 && s2->dpv < -25 && s3->dpv < -25;
		int f2 = s1->dpv < -50 && s2->dpv < -50;
		int f1 = s1->dpv < -100;
		if (f3 || f2 || f1) {
			pstate->flags |= FLAG_PV_FALLING;
			xdebug("SOLAR set FLAG_PV_FALLING 3=%d 2=%d 1=%d", s3->dpv, s2->dpv, s1->dpv);
		}
	}

	pthread_mutex_unlock(&collector_lock);

	// copy to history
	memcpy(PSTATE_SEC_NOW, (void*) pstate, sizeof(pstate_t));

	// print pstate once per minute / when delta / on grid load
	if (MINLY || PSTATE_DELTA || pstate->grid > NOISE)
		print_pstate();
}

static void daily() {
	xdebug("SOLAR collector executing daily tasks...");

	// calculate forecast errors - actual vs. expected
	int yday2 = now->tm_wday > 1 ? now->tm_wday - 2 : (now->tm_wday - 2 + 7);
	int fc2 = GSTATE_DAY_HOUR(yday2, 23)->tomorrow;
	int e2 = fc2 ? gstate->pv * 100 / fc2 : 0;
	xlog("SOLAR yesterdays 23:00 forecast for today %d, actual %d, strike %d%%", fc2, gstate->pv, e2);
	int yday1 = now->tm_wday > 0 ? now->tm_wday - 1 : (now->tm_wday - 1 + 7);
	int fc1 = GSTATE_DAY_HOUR(yday1, 7)->today;
	int e1 = fc1 ? gstate->pv * 100 / fc1 : 0;
	xlog("SOLAR     todays 07:00 forecast for today %d, actual %d, strike %d%%", fc1, gstate->pv, e1);

	// recalculate average 24/7 loads
	collect_loads();

	// store state at least once per day
	store_state();
	mosmix_store_state();

	// save pstate SVG
	char command[64], c = '0' + (now->tm_wday > 0 ? now->tm_wday - 1 : 6);
	snprintf(command, 64, "cp -f %s/pstate.svg %s/pstate-%c.svg", RUN, RUN, c);
	system(command);
	xdebug("SOLAR saved pstate SVG: %s", command);

	// recalculate mosmix factors
	mosmix_factors(0);
}

static void hourly() {
	xdebug("SOLAR collector executing hourly tasks...");

	// update forecasts and clear at midnight
	mosmix_load(now, WORK SLASH MARIENBERG, DAILY);

	// collect power to survive overnight
	gstate->need_survive = mosmix_survive(now, loads, BASELOAD, 25); // +25Wh inverter dissipation

	// collect sod errors and scale all remaining eod values, success factor before and after scaling in succ1/succ2
	int succ1, succ2;
	mosmix_scale(now, &succ1, &succ2);
	gstate->forecast = succ1;
	CUT(gstate->forecast, 2000);

#ifdef GNUPLOT
	// create fresh csv files and paint new diagrams
	store_table_csv((int*) GSTATE_TODAY, GSTATE_SIZE, 24, GSTATE_HEADER, RUN SLASH GSTATE_TODAY_CSV);
	store_table_csv((int*) gstate_history, GSTATE_SIZE, HISTORY_SIZE, GSTATE_HEADER, RUN SLASH GSTATE_WEEK_CSV);
	mosmix_store_csv();
	system(GNUPLOT);
#endif
}

static void minly() {
	// calculate counter and global state
	calculate_counter();
	calculate_gstate();
}

static void aggregate_state() {
	// reverse order: first aggregate hours, then minutes, then seconds
	if (MINLY) {
		if (HOURLY) {
			if (DAILY) {

				// aggregate 24 pstate hours into one day
				pstate_t pda, pdc;
				aggregate((int*) &pda, (int*) pstate_hours, PSTATE_SIZE, 24);
				cumulate((int*) &pdc, (int*) pstate_hours, PSTATE_SIZE, 24);
				dump_table((int*) pstate_hours, PSTATE_SIZE, 24, -1, "SOLAR pstate_hours", PSTATE_HEADER);
				dump_struct((int*) &pda, PSTATE_SIZE, "[ØØ]", 0);
				dump_struct((int*) &pdc, PSTATE_SIZE, "[++]", 0);
				// aggregate 24 gstate hours into one day
				gstate_t gda, gdc;
				aggregate((int*) &gda, (int*) GSTATE_YDAY, GSTATE_SIZE, 24);
				cumulate((int*) &gdc, (int*) GSTATE_YDAY, GSTATE_SIZE, 24);
				dump_table((int*) GSTATE_YDAY, GSTATE_SIZE, 24, -1, "SOLAR gstate_hours", GSTATE_HEADER);
				dump_struct((int*) &gda, GSTATE_SIZE, "[ØØ]", 0);
				dump_struct((int*) &gdc, GSTATE_SIZE, "[++]", 0);
			}

			// aggregate 60 minutes into one hour
			aggregate((int*) PSTATE_HOUR_NOW, (int*) pstate_minutes, PSTATE_SIZE, 60);
			// dump_table((int*) pstate_minutes, PSTATE_SIZE, 60, -1, "SOLAR pstate_minutes", PSTATE_HEADER);
			// dump_struct((int*) PSTATE_HOUR_NOW, PSTATE_SIZE, "[ØØ]", 0);

			// create/append gstate and pstate minutes to csv
			int offset = 60 * (now->tm_hour > 0 ? now->tm_hour - 1 : 23);
			if (!offset || access(RUN SLASH GSTATE_M_CSV, F_OK))
				store_csv_header(GSTATE_HEADER, RUN SLASH GSTATE_M_CSV);
			if (!offset || access(RUN SLASH PSTATE_M_CSV, F_OK))
				store_csv_header(PSTATE_HEADER, RUN SLASH PSTATE_M_CSV);
			append_table_csv((int*) gstate_minutes, GSTATE_SIZE, 60, offset, RUN SLASH GSTATE_M_CSV);
			append_table_csv((int*) pstate_minutes, PSTATE_SIZE, 60, offset, RUN SLASH PSTATE_M_CSV);
		}

		// aggregate 60 seconds into one minute
		aggregate((int*) PSTATE_MIN_NOW, (int*) pstate_seconds, PSTATE_SIZE, 60);
		// dump_table((int*) pstate_seconds, PSTATE_SIZE, 60, -1, "SOLAR pstate_seconds", PSTATE_HEADER);
		// dump_struct((int*) PSTATE_MIN_NOW, PSTATE_SIZE, "[ØØ]", 0);
	}
}

static void loop() {
	time_t now_ts;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// wait for tasmota discovery + sensor update
	sleep(1);

	// the SOLAR main loop
	while (1) {

		// get actual time and store global
		now_ts = time(NULL);
		localtime_r(&now_ts, &now_tm);

		// aggregate state values into day-hour-minute when 0-0-0
		aggregate_state();

		// calculate power state
		calculate_pstate();

		// cron jobs
		if (MINLY) {
			minly();

			if (HOURLY) {
				hourly();

				if (DAILY)
					daily();
			}
		}

		// web output
		create_pstate_json();
		create_gstate_json();
		create_powerflow_json();

		// wait for next second
		while (now_ts == time(NULL))
			msleep(111);
	}
}

static int init() {
	// initialize global time structure
	time_t now_ts = time(NULL);
	localtime_r(&now_ts, &now_tm);

	pthread_mutex_init(&collector_lock, NULL);

	load_state();
	mosmix_load_state(now);
	mosmix_load(now, WORK SLASH MARIENBERG, 0);
	collect_loads();

	return 0;
}

static void stop() {
	// saving state - this is the most important !!!
	store_state();
	mosmix_store_state();

	pthread_mutex_destroy(&collector_lock);
}

MCP_REGISTER(solar_collector, 11, &init, &stop, &loop);
