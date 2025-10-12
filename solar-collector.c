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

#define GNUPLOT_MINLY			"/usr/bin/gnuplot -p /var/lib/mcp/solar-minly.gp"
#define GNUPLOT_HOURLY			"/usr/bin/gnuplot -p /var/lib/mcp/solar-hourly.gp"

// hexdump -v -e '6 "%10d ""\n"' /var/lib/mcp/solar-counter*.bin
#define COUNTER_H_FILE			"solar-counter-hours.bin"
#define COUNTER_FILE			"solar-counter.bin"

// hexdump -v -e '21 "%6d ""\n"' /var/lib/mcp/solar-gstate*.bin
#define GSTATE_H_FILE			"solar-gstate-hours.bin"
#define GSTATE_M_FILE			"solar-gstate-minutes.bin"
#define GSTATE_FILE				"solar-gstate.bin"

// hexdump -v -e '31 "%6d ""\n"' /var/lib/mcp/solar-pstate*.bin
#define PSTATE_H_FILE			"solar-pstate-hours.bin"
#define PSTATE_M_FILE			"solar-pstate-minutes.bin"
#define PSTATE_S_FILE			"solar-pstate-seconds.bin"

// CSV files for gnuplot
#define GSTATE_TODAY_CSV		"gstate-today.csv"
#define GSTATE_WEEK_CSV			"gstate-week.csv"
#define GSTATE_M_CSV			"gstate-minutes.csv"
#define PSTATE_M_CSV			"pstate-minutes.csv"
#define PSTATE_S_CSV			"pstate-seconds.csv"
#define LOADS_CSV				"loads.csv"
#define AKKUS_CSV				"akkus.csv"

// JSON files for webui
#define PSTATE_JSON				"pstate.json"
#define GSTATE_JSON				"gstate.json"
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
#define PSTATE_SEC_NEXT			(&pstate_seconds[now->tm_sec < 59 ? now->tm_sec +  1 : 0])
#define PSTATE_SEC_LAST1		(&pstate_seconds[now->tm_sec >  0 ? now->tm_sec -  1 : 59])
#define PSTATE_SEC_LAST2		(&pstate_seconds[now->tm_sec >  1 ? now->tm_sec -  2 : (now->tm_sec -  2 + 60)])
#define PSTATE_SEC_LAST3		(&pstate_seconds[now->tm_sec >  2 ? now->tm_sec -  3 : (now->tm_sec -  3 + 60)])
#define PSTATE_SEC_LAST4		(&pstate_seconds[now->tm_sec >  3 ? now->tm_sec -  4 : (now->tm_sec -  4 + 60)])
#define PSTATE_SEC_LAST5		(&pstate_seconds[now->tm_sec >  4 ? now->tm_sec -  5 : (now->tm_sec -  5 + 60)])
#define PSTATE_SEC_LAST10		(&pstate_seconds[now->tm_sec >  9 ? now->tm_sec - 10 : (now->tm_sec - 10 + 60)])
#define PSTATE_MIN_NOW			(&pstate_minutes[now->tm_min])
#define PSTATE_MIN_LAST1		(&pstate_minutes[now->tm_min >  0 ? now->tm_min -  1 : 59])
#define PSTATE_MIN_LAST2		(&pstate_minutes[now->tm_min >  1 ? now->tm_min -  2 : (now->tm_min -  2 + 60)])
#define PSTATE_MIN_LAST3		(&pstate_minutes[now->tm_min >  2 ? now->tm_min -  3 : (now->tm_min -  3 + 60)])
#define PSTATE_HOUR_NOW			(&pstate_hours[now->tm_hour])
//#define PSTATE_HOUR_LAST1		(&pstate_hours[now->tm_hour > 0 ? now->tm_hour - 1 : 23])
#define PSTATE_HOUR(h)			(&pstate_hours[h])
#define PSTATE_MIN(m)			(&pstate_minutes[m])
#define PSTATE_SEC(s)			(&pstate_seconds[s])

// load and akku over 24/7
static int aloads[24], abatts[24];

static struct tm now_tm, *now = &now_tm;

// local counter/pstate/gstate/params memory
static counter_t counter_history[HISTORY_SIZE];
static gstate_t gstate_history[HISTORY_SIZE], gstate_minutes[60], gstate_current;
static pstate_t pstate_seconds[60], pstate_minutes[60], pstate_hours[24], pstate_current;
static params_t params_current;

// global counter/pstate/gstate/params pointer
counter_t counter[10];
gstate_t *gstate = &gstate_current;
pstate_t *pstate = &pstate_current;
params_t *params = &params_current;

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

	// Fronius expects negative load
	int load = pstate->aload * -1;

#define POWERFLOW_TEMPLATE		"{\"common\":{\"datestamp\":\"01.01.2025\",\"timestamp\":\"00:00:00\"},\"inverters\":[{\"BatMode\":1,\"CID\":0,\"DT\":0,\"E_Total\":1,\"ID\":1,\"P\":1,\"SOC\":%f}],\"site\":{\"BackupMode\":false,\"storeryStandby\":false,\"E_Day\":null,\"E_Total\":1,\"E_Year\":null,\"MLoc\":0,\"Mode\":\"bidirectional\",\"P_Akku\":%d,\"P_Grid\":%d,\"P_Load\":%d,\"P_PV\":%d,\"rel_Autonomy\":100.0,\"rel_SelfConsumption\":100.0},\"version\":\"13\"}"
	fprintf(fp, POWERFLOW_TEMPLATE, FLOAT10(gstate->soc), pstate->abatt, pstate->agrid, load, pstate->apv);
	fflush(fp);
	fclose(fp);
}

// paint new diagrams
static void gnuplot() {
#ifdef GNUPLOT_MINLY
	if (MINLY || access(RUN SLASH "pstate-seconds.svg", F_OK)) {
		// pstate seconds
		int offset = 60 * (now->tm_min > 0 ? now->tm_min - 1 : 59);
		if (!offset || access(RUN SLASH PSTATE_S_CSV, F_OK))
			store_csv_header(PSTATE_HEADER, RUN SLASH PSTATE_S_CSV);
		append_table_csv((int*) pstate_seconds, PSTATE_SIZE, 60, offset, RUN SLASH PSTATE_S_CSV);
		system(GNUPLOT_MINLY);
	}
#endif
#ifdef GNUPLOT_HOURLY
	if (HOURLY || access(RUN SLASH "pstate.svg", F_OK)) {
		// gstate and pstate minutes
		int offset = 60 * (now->tm_hour > 0 ? now->tm_hour - 1 : 23);
		if (!offset || access(RUN SLASH GSTATE_M_CSV, F_OK))
			store_csv_header(GSTATE_HEADER, RUN SLASH GSTATE_M_CSV);
		if (!offset || access(RUN SLASH PSTATE_M_CSV, F_OK))
			store_csv_header(PSTATE_HEADER, RUN SLASH PSTATE_M_CSV);
		append_table_csv((int*) gstate_minutes, GSTATE_SIZE, 60, offset, RUN SLASH GSTATE_M_CSV);
		append_table_csv((int*) pstate_minutes, PSTATE_SIZE, 60, offset, RUN SLASH PSTATE_M_CSV);
		// gstate today and week
		store_table_csv((int*) GSTATE_TODAY, GSTATE_SIZE, 24, GSTATE_HEADER, RUN SLASH GSTATE_TODAY_CSV);
		store_table_csv((int*) gstate_history, GSTATE_SIZE, HISTORY_SIZE, GSTATE_HEADER, RUN SLASH GSTATE_WEEK_CSV);
		// mosmix today and tomorrow
		mosmix_store_csv();
		system(GNUPLOT_HOURLY);
	}
#endif
	// TODO sensors plot
}

static void collect_averages() {
	char line[LINEBUF], value[10];

	ZERO(aloads);
	for (int h = 0; h < 24; h++) {
		for (int d = 0; d < 7; d++) {
			gstate_t *g = GSTATE_DAY_HOUR(d, h);
			aloads[h] += g->load;
			abatts[h] += g->batt;
		}
		aloads[h] /= 7;
		abatts[h] /= 7;
	}

	strcpy(line, "SOLAR average 24/7 load:");
	for (int h = 0; h < 24; h++) {
		snprintf(value, 10, " %d", aloads[h]);
		strcat(line, value);
	}
	xlog(line);

	strcpy(line, "SOLAR average 24/7 akku:");
	for (int h = 0; h < 24; h++) {
		snprintf(value, 10, " %d", abatts[h]);
		strcat(line, value);
	}
	xlog(line);

	store_array_csv(aloads, 24, 1, "  load", RUN SLASH LOADS_CSV);
	store_array_csv(abatts, 24, 1, "  akku", RUN SLASH AKKUS_CSV);

	// calculate nightly baseload
	params->baseload = round10((aloads[3] + aloads[4] + aloads[5]) / 3);
	// TODO remove after 1week
	params->baseload = BASELOAD;
}

static void print_gstate() {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "GSTATE ");
	xlogl_bits16(line, NULL, gstate->flags);
	xlogl_int_b(line, "∑PV", gstate->pv);
	xlogl_int_noise(line, NOISE, 0, "↑Grid", gstate->produced);
	xlogl_int_noise(line, NOISE, 1, "↓Grid", gstate->consumed);
	xlogl_percent10(line, "Succ", gstate->success);
	xlogl_percent10(line, "Surv", gstate->survive);
//	xlogl_float(line, "SoC", FLOAT10(gstate->soc));
//	xlogl_float(line, "TTL", FLOAT60(gstate->ttl));
//	xlogl_float(line, "Ti", sensors->tin);
//	xlogl_float(line, "To", sensors->tout);
//	xlogl_int(line, "Today", gstate->today);
//	xlogl_int(line, "Tomo", gstate->tomorrow);
//	xlogl_int(line, "SoD", gstate->sod);
//	xlogl_int(line, "EoD", gstate->eod);
	if (!PSTATE_OFFLINE) {
		xlogl_int(line, "PVmin", gstate->pvmin);
		xlogl_int(line, "PVavg", gstate->pvavg);
		xlogl_int(line, "PVmax", gstate->pvmax);
	}
	xlogl_end(line, strlen(line), 0);
}

static void print_pstate() {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "PSTATE ");
	xlogl_bits16(line, NULL, pstate->flags);
	xlogl_int(line, "Inv", pstate->inv);
	xlogl_int(line, "Load", pstate->load);
	xlogl_int_noise(line, NOISE, 1, "Batt", pstate->batt);
	xlogl_int_noise(line, NOISE, 1, "Grid", pstate->grid);
	if (!PSTATE_OFFLINE) {
		xlogl_int(line, "PV10", pstate->mppt1 + pstate->mppt2);
		xlogl_int(line, "PV7", pstate->mppt3 + pstate->mppt4);
		xlogl_int_noise(line, NOISE, 0, "Surp", pstate->surp);
	}
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
		xlog("SOLAR counter meter cons=%d prod=%d 1=%d 2=%d 3=%d 4=%d", CM_HOUR->consumed, CM_HOUR->produced, CM_HOUR->mppt1, CM_HOUR->mppt2, CM_HOUR->mppt3, CM_HOUR->mppt4);
		xlog("SOLAR counter self  cons=%d prod=%d 1=%d 2=%d 3=%d 4=%d", CS_HOUR->consumed, CS_HOUR->produced, CS_HOUR->mppt1, CS_HOUR->mppt2, CS_HOUR->mppt3, CS_HOUR->mppt4);

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
			xlog("SOLAR counter meter  cons=%d prod=%d 1=%d 2=%d 3=%d 4=%d", CM_DAY->consumed, CM_DAY->produced, CM_DAY->mppt1, CM_DAY->mppt2, CM_DAY->mppt3, CM_DAY->mppt4);
			xlog("SOLAR counter self   cons=%d prod=%d 1=%d 2=%d 3=%d 4=%d", CS_DAY->consumed, CS_DAY->produced, CS_DAY->mppt1, CS_DAY->mppt2, CS_DAY->mppt3, CS_DAY->mppt4);

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

	// store values in gstate for 24/7 average calculation
	gstate->load = PSTATE_HOUR_NOW->load;
	gstate->batt = PSTATE_HOUR_NOW->batt;

	// calculate pv minimum, maximum and average
	gstate->pvmin = UINT16_MAX;
	gstate->pvmax = 0, gstate->pvavg = 0;
	for (int m = 0; m < 60; m++) {
		pstate_t *p = PSTATE_MIN(m);
		HICUT(gstate->pvmin, p->pv)
		LOCUT(gstate->pvmax, p->pv)
		gstate->pvavg += p->pv;
	}
	gstate->pvavg /= 60;
	gstate->pvmin += gstate->pvmin / 10; // +10%
	gstate->pvmax -= gstate->pvmax / 10; // -10%
	gstate->pvmin = round100(gstate->pvmin);
	gstate->pvmax = round100(gstate->pvmax);
	gstate->pvavg = round100(gstate->pvavg);

	// akku usable energy and estimated time to live based on last hour's average load +5% extra +inverter dissipation
	int min = akku_get_min_soc();
	int akku = AKKU_AVAILABLE;
	gstate->ttl = gstate->load && gstate->soc > min ? akku * 60 / (gstate->load + gstate->load / 20 + pstate->diss) : 0;

	// collect mosmix forecasts
	int today, tomorrow, sod, eod;
	mosmix_collect(now, &tomorrow, &today, &sod, &eod);
	gstate->tomorrow = tomorrow;
	gstate->today = today;
	gstate->sod = sod;
	gstate->eod = eod;
	gstate->success = sod > MINIMUM && gstate->pv > NOISE ? gstate->pv * 1000 / sod : 0;
	HICUT(gstate->success, 2000)
	xdebug("SOLAR pv=%d sod=%d eod=%d success=%.1f%%", gstate->pv, sod, eod, FLOAT10(gstate->success));

	// survival factor
	int tocharge = gstate->nsurvive - akku;
	LOCUT(tocharge, 0)
	int available = gstate->eod - tocharge;
	LOCUT(available, 0)
	if (gstate->sod == 0)
		available = 0; // pv not yet started - we only have akku
	gstate->survive = gstate->nsurvive ? (available + akku) * 1000 / gstate->nsurvive : 0;
	HICUT(gstate->survive, 2000)
	xdebug("SOLAR survive eod=%d tocharge=%d avail=%d akku=%d need=%d --> %.1f%%", gstate->eod, tocharge, available, akku, gstate->nsurvive, FLOAT10(gstate->survive));

	// heating
	gstate->flags |= FLAG_HEATING;
	// no need to heat
	if (sensors->tin > 18.0 && SUMMER)
		gstate->flags &= ~FLAG_HEATING;
	if (sensors->tin > 24.0 && sensors->tout > 15.0 && !SUMMER)
		gstate->flags &= ~FLAG_HEATING;
	if (sensors->tin > 26.0)
		gstate->flags &= ~FLAG_HEATING;
	// force heating
	if ((now->tm_mon == 4 || now->tm_mon == 8) && now->tm_hour >= 16 && sensors->tin < 25.0) // may/sept begin 16 o'clock
		gstate->flags |= FLAG_HEATING;
	else if ((now->tm_mon == 3 || now->tm_mon == 9) && now->tm_hour >= 14 && sensors->tin < 25.0) // apr/oct begin 14 o'clock
		gstate->flags |= FLAG_HEATING;
	else if ((now->tm_mon < 3 || now->tm_mon > 9) && sensors->tin < 28.0) // nov-mar always
		gstate->flags |= FLAG_HEATING;

	// akku charging
	int empty = gstate->soc < 100;
	int soc6 = GSTATE_HOUR(6)->soc;
	int time_window = now->tm_hour >= 9 && now->tm_hour < 15; // between 9 and 15 o'clock
	int weekend = (now->tm_wday == 5 || now->tm_wday == 6) && gstate->soc < 500 && !SUMMER; // Friday+Saturday: akku has to be at least 50%
	if (empty || weekend || WINTER)
		// empty or weekend or winter: always at any time
		gstate->flags |= FLAG_CHARGE_AKKU;
	else if (SUMMER) {
		// summer: when below 22%
		if (time_window && soc6 < 222)
			gstate->flags |= FLAG_CHARGE_AKKU;
	} else {
		// autumn/spring: when below 33% or tomorrow not enough pv
		if (time_window && soc6 < 333)
			gstate->flags |= FLAG_CHARGE_AKKU;
		if (time_window && gstate->tomorrow < params->akku_capacity * 2)
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
	if (!inv1)
		pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = pstate->batt = 0;
	if (!inv2)
		pstate->ac2 = pstate->dc2 = pstate->mppt3 = pstate->mppt4;

	// clear state flags and values
	pstate->flags = 0;

	// update self counter before shaping
	if (pstate->grid > 0)
		CS_NOW->consumed += pstate->grid;
	if (pstate->grid < 0)
		CS_NOW->produced += pstate->grid * -1;
	CS_NOW->mppt1 += pstate->mppt1;
	CS_NOW->mppt2 += pstate->mppt2;
	CS_NOW->mppt3 += pstate->mppt3;
	CS_NOW->mppt4 += pstate->mppt4;

	// history states
	pstate_t *s1 = PSTATE_SEC_LAST1;
	pstate_t *s2 = PSTATE_SEC_LAST2;
	pstate_t *s3 = PSTATE_SEC_LAST3;
	pstate_t *s4 = PSTATE_SEC_LAST4;
	pstate_t *s5 = PSTATE_SEC_LAST5;
	pstate_t *s10 = PSTATE_SEC_LAST10;
	pstate_t *m0 = PSTATE_MIN_NOW;
	pstate_t *m1 = PSTATE_MIN_LAST1;
	pstate_t *m2 = PSTATE_MIN_LAST2;

	// dissipation
	int diss1 = pstate->dc1 - pstate->ac1;
	int diss2 = pstate->dc2 - pstate->ac2;
	pstate->diss = diss1 + diss2;
	AVERAGE(pstate->adiss, pstate->diss, s1->diss)
	// xdebug("SOLAR Inverter Dissipation diss1=%d diss2=%d adiss=%d", diss1, diss2, pstate->adiss);

	// pv
	ZSHAPE(pstate->mppt1, NOISE)
	ZSHAPE(pstate->mppt2, NOISE)
	ZSHAPE(pstate->mppt3, NOISE)
	ZSHAPE(pstate->mppt4, NOISE)
	pstate->pv = pstate->mppt1 + pstate->mppt2 + pstate->mppt3 + pstate->mppt4;
	DELTAZ(pstate->dpv, pstate->pv, s1->pv, DELTA)
	AVERAGE(pstate->apv, pstate->pv, s1->pv)

	// grid
	DELTAZ(pstate->dgrid, pstate->grid, s1->grid, DELTA)
	AVERAGE(pstate->agrid, pstate->grid, s1->grid)

	// akku
	AVERAGE(pstate->abatt, pstate->batt, s1->batt)

	// load - use ac values 4 seconds ago due to inverter balancing after grid change - check nightly akku service interval -> nearly no load change
	pstate->load = pstate->agrid + s4->ac1 + s4->ac2;
	AVERAGE(pstate->aload, pstate->load, s1->load)

	// ratio pv / load - only when we have pv and load
	pstate->pload = pstate->apv && pstate->aload ? (pstate->apv - pstate->adiss) * 100 / pstate->aload : 0;
	LOCUT(pstate->pload, 0)

	// check if we have delta ac power anywhere
	if (abs(pstate->grid - s1->grid) > DELTA)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac1 - s1->ac1) > DELTA)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac2 - s1->ac2) > DELTA)
		pstate->flags |= FLAG_DELTA;

	// grid upload in last 3 minutes
	if (pstate->grid < -50) {
		int g2 = m0->grid < -50 && m1->grid < -50 && m2->grid < -50;
		int g1 = m0->grid < -75 && m1->grid < -75;
		int g0 = m0->grid < -100;
		if (g2 || g1 || g0) {
			pstate->flags |= FLAG_GRID_ULOAD;
			xdebug("SOLAR set FLAG_GRID_ULOAD last 3=%d 2=%d 1=%d", m2->grid, m1->grid, m0->grid);
		}
	}

	// grid download in last 3 minutes
	if (pstate->grid > 50) {
		int g2 = m0->grid > 50 && m1->grid > 50 && m2->grid > 50;
		int g1 = m0->grid > 75 && m1->grid > 75;
		int g0 = m0->grid > 100;
		if (g2 || g1 || g0) {
			pstate->flags |= FLAG_GRID_DLOAD;
			xdebug("SOLAR set FLAG_GRID_DLOAD last 3=%d 2=%d 1=%d", m2->grid, m1->grid, m0->grid);
		}
	}

	// akku discharge in last 3 minutes
	if (pstate->batt > NOISE) {
		int a2 = m0->batt > 50 && m1->batt > 50 && m2->batt > 50;
		int a1 = m0->batt > 75 && m1->batt > 75;
		int a0 = m0->batt > 100;
		if (a2 || a1 || a0) {
			pstate->flags |= FLAG_AKKU_DCHARGE;
			xdebug("SOLAR set FLAG_AKKU_DCHARGE last 3=%d 2=%d 1=%d", m2->batt, m1->batt, m0->batt);
		}
	}

	// offline mode when PV / load ratio is last 3 minutes below 100%
	int offline = m0->pload < 100 && m1->pload < 100 && m2->pload < 100;
	if (offline) {

		// akku burn out between 6 and 9 o'clock if we can re-charge it completely by day
		int burnout_time = now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8;
		int burnout_possible = sensors->tin < 18.0 && gstate->soc > 150;
		if (burnout_time && burnout_possible && AKKU_BURNOUT)
			pstate->flags |= FLAG_BURNOUT; // burnout
		else
			pstate->flags |= FLAG_OFFLINE; // offline

		pstate->surp = 0;

	} else {
		// online

		// emergency shutdown: grid download at all states or akku discharge at one of them
		int egrid = pstate->grid > EMERGENCY && s1->grid > EMERGENCY && s2->grid > EMERGENCY && s3->grid > EMERGENCY;
		int eakku = pstate->batt > EMERGENCY || s5->batt > EMERGENCY || s10->batt > EMERGENCY;
		if (egrid || eakku) {
			pstate->flags |= FLAG_EMERGENCY;
			xlog("SOLAR set FLAG_EMERGENCY egrid=%d eakku=%d", egrid, eakku);
		}

		// first set and then clear VALID flag when values suspicious
		pstate->flags |= FLAG_VALID;

		// meter latency / mppt tracking / too fast pv delta / grid spikes / etc.
		// TODO anpassen nach korrigierter Berechnung - s3 values nehmen?
		int sum = pstate->pv + pstate->grid + pstate->batt + pstate->load * -1;
		if (abs(sum) > SUSPICIOUS) {
			xlog("SOLAR suspicious inverter values detected: sum=%d", sum);
			pstate->flags &= ~FLAG_VALID;
		}
		int psum = pstate->p1 + pstate->p2 + pstate->p3;
		if (psum < pstate->grid - NOISE || psum > pstate->grid + NOISE) {
			xlog("SOLAR suspicious meter values detected p1=%d p2=%d p3=%d sum=%d grid=%d", pstate->p1, pstate->p2, pstate->p3, psum, pstate->grid);
			pstate->flags &= ~FLAG_VALID;
		}
		int dp1 = pstate->p1 - s1->p1;
		int dp2 = pstate->p2 - s1->p2;
		int dp3 = pstate->p3 - s1->p3;
		if (abs(pstate->dgrid) > SPIKE || abs(dp1) > SPIKE || abs(dp2) > SPIKE || abs(dp3) > SPIKE) {
			xlog("SOLAR grid spike detected dgrid=%d dp1=%d dp2=%d dp3=%d", pstate->dgrid, dp1, dp2, dp3);
			pstate->flags &= ~FLAG_VALID;
		}
		if (pstate->load < 0) {
			xlog("SOLAR negative load detected %d", pstate->load);
			pstate->flags &= ~FLAG_VALID;
		}
		if (pstate->grid < -NOISE && pstate->batt > NOISE) {
			int waste = abs(pstate->grid) < pstate->batt ? abs(pstate->grid) : pstate->batt;
			xlog("SOLAR wasting power %d akku -> grid", waste);
			pstate->flags &= ~FLAG_VALID;
		}
		if (inv1 != I_STATUS_MPPT) {
			xlog("SOLAR Inverter1 state %d expected %d", inv1, I_STATUS_MPPT);
			pstate->flags &= ~FLAG_VALID;
		}
		if (inv2 != I_STATUS_MPPT) {
			xlog("SOLAR Inverter2 state %d expected %d ", inv2, I_STATUS_MPPT);
			pstate->flags &= ~FLAG_VALID;
		}

		// state is stable when we have now and last 3s no grid changes
		if (!pstate->dgrid && !s1->dgrid && !s2->dgrid && !s3->dgrid)
			pstate->flags |= FLAG_STABLE;

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

		// load is completely satisfied from secondary inverter
		if ((-NOISE < pstate->ac1 && pstate->ac1 < NOISE) || pstate->load < pstate->ac2) {
			pstate->flags |= FLAG_EXTRAPOWER;
			xdebug("SOLAR set FLAG_EXTRAPOWER load=%d ac1=%d ac2=%d", pstate->load, pstate->ac1, pstate->ac2);
		}

		// TODO mppt tracking erkennen und ramping verhindern, zuerst geht ac runter, dann dc und mpptx, 1-2 sekunden später grid rückmeldung
		// und eine sekunde später alles wieder rauf

		// TODO wolken: es müssen beide ac / dc values gemeinsam runter gehen -> sofort delta ramp runter wenn pv < load

		// TODO maximalen surplus basierend auf pvmin/pvmax/pvavg setzen z.B. wenn delta pvmin/pvmax zu groß -> ersetzt DISTORTION logik

		// calculate surplus power - here we use average values

		// surplus is grid inverted minus akku when discharging
		int surp = pstate->agrid * -1;
		if (pstate->abatt > NOISE)
			surp -= pstate->abatt * -1;

		if (pstate->pload >= 110) {

			// enough - suppress when below 0
			LOCUT(surp, 0)

		} else if (100 <= pstate->pload && pstate->pload < 110) {

			// nearly equal - one step down to avoid grid download
			if (0 < pstate->agrid && pstate->agrid < RAMP)
				surp = -RAMP;
			// limit to +RAMP
			HICUT(surp, RAMP)

		} else {

			// not enough - suppress when above 0
			HICUT(surp, 0)

		}

		// shape + suppress spikes
		ZSHAPE(surp, RAMP)
		AVERAGE(pstate->surp, surp, s1->surp)
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

	// recalculate average 24/7 values
	collect_averages();

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
	gstate->nsurvive = mosmix_survive(now, aloads, abatts);

	// collect sod errors and scale all remaining eod values, success factor before and after scaling in succ1/succ2
	int succ1, succ2;
	mosmix_scale(now, &succ1, &succ2);
	gstate->forecast = succ1;
	HICUT(gstate->forecast, 2000);
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

	// collector main loop
	while (1) {

		// PROFILING_START

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
		gnuplot();

		// PROFILING_LOG("collector main loop")

		// wait for next second
		while (now_ts == time(NULL))
			msleep(222);
	}
}

static int init() {
	// initialize global time structure
	time_t now_ts = time(NULL);
	localtime_r(&now_ts, &now_tm);

	pthread_mutex_init(&collector_lock, NULL);

	// clear pstate
	ZERO(pstate_current);

	load_state();
	mosmix_load_state(now);
	mosmix_load(now, WORK SLASH MARIENBERG, 0);
	collect_averages();

	return 0;
}

static void stop() {
	// saving state - this is the most important !!!
	store_state();
	mosmix_store_state();

	pthread_mutex_destroy(&collector_lock);
}

MCP_REGISTER(solar_collector, 11, &init, &stop, &loop);
