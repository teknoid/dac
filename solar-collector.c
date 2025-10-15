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
#define AVERAGE					10

#define GNUPLOT_MINLY			"/usr/bin/gnuplot -p /var/lib/mcp/solar-minly.gp"
#define GNUPLOT_HOURLY			"/usr/bin/gnuplot -p /var/lib/mcp/solar-hourly.gp"

// hexdump -v -e '6 "%10d ""\n"' /var/lib/mcp/solar-counter*.bin
#define COUNTER_H_FILE			"solar-counter-hours.bin"
#define COUNTER_FILE			"solar-counter.bin"

// hexdump -v -e '17 "%6d ""\n"' /var/lib/mcp/solar-gstate*.bin
#define GSTATE_H_FILE			"solar-gstate-hours.bin"
#define GSTATE_M_FILE			"solar-gstate-minutes.bin"
#define GSTATE_FILE				"solar-gstate.bin"

// hexdump -v -e '26 "%6d ""\n"' /var/lib/mcp/solar-pstate*.bin
#define PSTATE_H_FILE			"solar-pstate-hours.bin"
#define PSTATE_M_FILE			"solar-pstate-minutes.bin"
#define PSTATE_S_FILE			"solar-pstate-seconds.bin"
#define PSTATE_A_FILE			"solar-pstate-average.bin"

// CSV files for gnuplot
#define GSTATE_TODAY_CSV		"gstate-today.csv"
#define GSTATE_WEEK_CSV			"gstate-week.csv"
#define GSTATE_M_CSV			"gstate-minutes.csv"
#define PSTATE_M_CSV			"pstate-minutes.csv"
#define PSTATE_S_CSV			"pstate-seconds.csv"
#define PSTATE_A_247_CSV		"pstate-average-247.csv"
#define PSTATE_WEEK_CSV			"pstate-week.csv"
#define LOADS_CSV				"loads.csv"
#define AKKUS_CSV				"akkus.csv"

// JSON files for webui
#define PSTATE_AVG_JSON			"pstate-avg.json"
#define PSTATE_JSON				"pstate.json"
#define GSTATE_JSON				"gstate.json"
#define POWERFLOW_JSON			"powerflow.json"

// counter history
#define COUNTER_HOUR_NOW		(&counter_hours[24 * now->tm_wday + now->tm_hour])

// gstate access pointers
#define GSTATE_MIN_NOW			(&gstate_minutes[now->tm_min])
#define GSTATE_MIN_LAST			(&gstate_minutes[now->tm_min > 0 ? now->tm_min - 1 : 59])
#define GSTATE_HOUR_NOW			(&gstate_hours[24 * now->tm_wday + now->tm_hour])
#define GSTATE_HOUR_LAST		(&gstate_hours[24 * now->tm_wday + now->tm_hour - (now->tm_wday == 0 && now->tm_hour ==  0 ?  24 * 7 - 1 : 1)])
#define GSTATE_HOUR_NEXT		(&gstate_hours[24 * now->tm_wday + now->tm_hour + (now->tm_wday == 6 && now->tm_hour == 23 ? -24 * 7 + 1 : 1)])
#define GSTATE_TODAY			(&gstate_hours[24 * now->tm_wday])
#define GSTATE_YDAY				(&gstate_hours[24 * (now->tm_wday > 0 ? now->tm_wday - 1 : 6)])
#define GSTATE_HOUR(h)			(&gstate_hours[24 * now->tm_wday + (h)])
#define GSTATE_DAY_HOUR(d, h)	(&gstate_hours[24 * (d) + (h)])

// pstate access pointers
#define PSTATE_SEC_NOW			(&pstate_seconds[now->tm_sec])
#define PSTATE_SEC_NEXT			(&pstate_seconds[now->tm_sec < 59 ? now->tm_sec +  1 : 0])
#define PSTATE_SEC_LAST1		(&pstate_seconds[now->tm_sec >  0 ? now->tm_sec -  1 : 59])
#define PSTATE_SEC_LAST2		(&pstate_seconds[now->tm_sec >  1 ? now->tm_sec -  2 : (now->tm_sec -  2 + 60)])
#define PSTATE_SEC_LAST3		(&pstate_seconds[now->tm_sec >  2 ? now->tm_sec -  3 : (now->tm_sec -  3 + 60)])
#define PSTATE_SEC_LAST5		(&pstate_seconds[now->tm_sec >  4 ? now->tm_sec -  5 : (now->tm_sec -  5 + 60)])
#define PSTATE_SEC_LAST10		(&pstate_seconds[now->tm_sec >  9 ? now->tm_sec - 10 : (now->tm_sec - 10 + 60)])
#define PSTATE_MIN_NOW			(&pstate_minutes[now->tm_min])
#define PSTATE_MIN_LAST1		(&pstate_minutes[now->tm_min >  0 ? now->tm_min -  1 : 59])
#define PSTATE_MIN_LAST2		(&pstate_minutes[now->tm_min >  1 ? now->tm_min -  2 : (now->tm_min -  2 + 60)])
#define PSTATE_MIN_LAST3		(&pstate_minutes[now->tm_min >  2 ? now->tm_min -  3 : (now->tm_min -  3 + 60)])
#define PSTATE_MIN(m)			(&pstate_minutes[m])
#define PSTATE_HOUR_NOW			(&pstate_hours[24 * now->tm_wday + now->tm_hour])
#define PSTATE_YDAY				(&pstate_hours[24 * (now->tm_wday > 0 ? now->tm_wday - 1 : 6)])
#define PSTATE_DAY_HOUR(d, h)	(&pstate_hours[24 * (d) + (h)])
#define PSTATE_SEC(s)			(&pstate_seconds[s])
#define PSTATE_AVG				(&pstate_average[now->tm_sec])
#define PSTATE_AVG_247(h)		(&pstate_average_247[h])

static struct tm now_tm, *now = &now_tm;

// local counter/pstate/gstate/params memory
static counter_t counter_hours[HISTORY_SIZE];
static gstate_t gstate_hours[HISTORY_SIZE], gstate_minutes[60], gstate_current;
static pstate_t pstate_hours[HISTORY_SIZE], pstate_minutes[60], pstate_seconds[60], pstate_average[60], pstate_average_247[24], pstate_current;
static params_t params_current;

// local average pstate pointer
static pstate_t *pstate_avg = &pstate_average[0];

// global counter/pstate/gstate/params pointer
counter_t counter[10];
gstate_t *gstate = &gstate_current;
pstate_t *pstate = &pstate_current;
params_t *params = &params_current;

// mutex for updating / calculating pstate and counter
pthread_mutex_t collector_lock;

static void load_state() {
	load_blob(STATE SLASH COUNTER_FILE, counter, sizeof(counter));
	load_blob(STATE SLASH COUNTER_H_FILE, counter_hours, sizeof(counter_hours));

	load_blob(STATE SLASH GSTATE_H_FILE, gstate_hours, sizeof(gstate_hours));
	load_blob(STATE SLASH GSTATE_M_FILE, gstate_minutes, sizeof(gstate_minutes));
	load_blob(STATE SLASH GSTATE_FILE, gstate, sizeof(gstate_current));

	load_blob(STATE SLASH PSTATE_H_FILE, pstate_hours, sizeof(pstate_hours));
	load_blob(STATE SLASH PSTATE_M_FILE, pstate_minutes, sizeof(pstate_minutes));
	load_blob(STATE SLASH PSTATE_S_FILE, pstate_seconds, sizeof(pstate_seconds));
	load_blob(STATE SLASH PSTATE_A_FILE, pstate_average, sizeof(pstate_average));
}

static void store_state() {
	store_blob(STATE SLASH COUNTER_FILE, counter, sizeof(counter));
	store_blob(STATE SLASH COUNTER_H_FILE, counter_hours, sizeof(counter_hours));

	store_blob(STATE SLASH GSTATE_H_FILE, gstate_hours, sizeof(gstate_hours));
	store_blob(STATE SLASH GSTATE_M_FILE, gstate_minutes, sizeof(gstate_minutes));
	store_blob(STATE SLASH GSTATE_FILE, gstate, sizeof(gstate_current));

	store_blob(STATE SLASH PSTATE_H_FILE, pstate_hours, sizeof(pstate_hours));
	store_blob(STATE SLASH PSTATE_M_FILE, pstate_minutes, sizeof(pstate_minutes));
	store_blob(STATE SLASH PSTATE_S_FILE, pstate_seconds, sizeof(pstate_seconds));
	store_blob(STATE SLASH PSTATE_A_FILE, pstate_average, sizeof(pstate_average));
}

static void create_pstate_json() {
	store_array_json(pstate_avg, PSTATE_SIZE, PSTATE_HEADER, RUN SLASH PSTATE_AVG_JSON);
	store_array_json(pstate, PSTATE_SIZE, PSTATE_HEADER, RUN SLASH PSTATE_JSON);
}

static void create_gstate_json() {
	store_array_json(gstate, GSTATE_SIZE, GSTATE_HEADER, RUN SLASH GSTATE_JSON);
}

// feed Fronius powerflow web application
static void create_powerflow_json() {
	FILE *fp = fopen(RUN SLASH POWERFLOW_JSON, "wt");
	if (fp == NULL)
		return;

	// Fronius expects negative load
	int load = pstate->load * -1;

#define POWERFLOW_TEMPLATE		"{\"common\":{\"datestamp\":\"01.01.2025\",\"timestamp\":\"00:00:00\"},\"inverters\":[{\"BatMode\":1,\"CID\":0,\"DT\":0,\"E_Total\":1,\"ID\":1,\"P\":1,\"SOC\":%f}],\"site\":{\"BackupMode\":false,\"storeryStandby\":false,\"E_Day\":null,\"E_Total\":1,\"E_Year\":null,\"MLoc\":0,\"Mode\":\"bidirectional\",\"P_Akku\":%d,\"P_Grid\":%d,\"P_Load\":%d,\"P_PV\":%d,\"rel_Autonomy\":100.0,\"rel_SelfConsumption\":100.0},\"version\":\"13\"}"
	fprintf(fp, POWERFLOW_TEMPLATE, FLOAT10(gstate->soc), pstate->batt, pstate->grid, load, pstate->pv);
	fflush(fp);
	fclose(fp);
}

// paint new diagrams
static void gnuplot() {
#ifdef GNUPLOT_MINLY
	if (MINLY || access(RUN SLASH PSTATE_S_CSV, F_OK)) {
		// pstate seconds
		int offset = 60 * (now->tm_min > 0 ? now->tm_min - 1 : 59);
		if (!offset || access(RUN SLASH PSTATE_S_CSV, F_OK))
			store_csv_header(PSTATE_HEADER, RUN SLASH PSTATE_S_CSV);
		append_table_csv(pstate_average, PSTATE_SIZE, 60, offset, RUN SLASH PSTATE_S_CSV);
		system(GNUPLOT_MINLY);
	}
#endif
#ifdef GNUPLOT_HOURLY
	if (HOURLY || access(RUN SLASH PSTATE_M_CSV, F_OK)) {
		// gstate and pstate minutes
		int offset = 60 * (now->tm_hour > 0 ? now->tm_hour - 1 : 23);
		if (!offset || access(RUN SLASH GSTATE_M_CSV, F_OK))
			store_csv_header(GSTATE_HEADER, RUN SLASH GSTATE_M_CSV);
		if (!offset || access(RUN SLASH PSTATE_M_CSV, F_OK))
			store_csv_header(PSTATE_HEADER, RUN SLASH PSTATE_M_CSV);
		append_table_csv(gstate_minutes, GSTATE_SIZE, 60, offset, RUN SLASH GSTATE_M_CSV);
		append_table_csv(pstate_minutes, PSTATE_SIZE, 60, offset, RUN SLASH PSTATE_M_CSV);
		// gstate today and week
		store_table_csv(GSTATE_TODAY, GSTATE_SIZE, 24, GSTATE_HEADER, RUN SLASH GSTATE_TODAY_CSV);
		store_table_csv(gstate_hours, GSTATE_SIZE, HISTORY_SIZE, GSTATE_HEADER, RUN SLASH GSTATE_WEEK_CSV);
		// mosmix today and tomorrow
		mosmix_store_csv();
		system(GNUPLOT_HOURLY);
	}
#endif
	// TODO sensors plot
}

static void collect_average_247() {
	char line[LINEBUF], value[10];

	ZERO(pstate_average_247);
	for (int h = 0; h < 24; h++) {
		for (int d = 0; d < 7; d++) {
			int *dptr = (int*) PSTATE_AVG_247(h), *sptr = (int*) PSTATE_DAY_HOUR(d, h);
			for (int x = 0; x < PSTATE_SIZE; x++)
				*dptr++ += *sptr++;
		}
		int *ptr = (int*) PSTATE_AVG_247(h);
		for (int x = 0; x < PSTATE_SIZE; x++) {
			int z = *ptr * 10 / 7;
			*ptr++ = z / 10 + z % 10 < 5 ? 0 : 1;
		}
	}

	strcpy(line, "SOLAR average 24/7 load:");
	for (int h = 0; h < 24; h++) {
		snprintf(value, 10, " %d", PSTATE_AVG_247(h)->load);
		strcat(line, value);
	}
	xlog(line);

	strcpy(line, "SOLAR average 24/7 akku:");
	for (int h = 0; h < 24; h++) {
		snprintf(value, 10, " %d", PSTATE_AVG_247(h)->batt);
		strcat(line, value);
	}
	xlog(line);

	store_table_csv(pstate_average_247, PSTATE_SIZE, 24, PSTATE_HEADER, RUN SLASH PSTATE_A_247_CSV);

	// calculate nightly baseload
	int load3 = PSTATE_AVG_247(3)->load, load4 = PSTATE_AVG_247(4)->load, load5 = PSTATE_AVG_247(5)->load;
	params->baseload = round10(load3 + load4 + load5 / 3);
	// TODO remove after one week
	params->baseload = BASELOAD;
	xlog("SOLAR baseload=%d", params->baseload);
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
	if (!GSTATE_OFFLINE) {
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
	if (!GSTATE_OFFLINE) {
		xlogl_int(line, "PV10", pstate->mppt1 + pstate->mppt2);
		xlogl_int(line, "PV7", pstate->mppt3 + pstate->mppt4);
		xlogl_int(line, "PLoad", pstate->pload);
		xlogl_int_noise(line, NOISE, 0, "Ramp", pstate->ramp);
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
		memcpy(COUNTER_HOUR_NOW, CM_NOW, sizeof(counter_t));
		mosmix_mppt(now, CM_HOUR->mppt1, CM_HOUR->mppt2, CM_HOUR->mppt3, CM_HOUR->mppt4);
#else
		memcpy(COUNTER_HOUR_NOW, CS_NOW, sizeof(counter_t));
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

	// history states
	pstate_t *m0 = PSTATE_MIN_NOW;
	pstate_t *m1 = PSTATE_MIN_LAST1;
	pstate_t *m2 = PSTATE_MIN_LAST2;

	// offline mode when PV / load ratio is last 3 minutes below 75%
	int offline = m0->pload < 75 && m1->pload < 75 && m2->pload < 75;
	if (offline) {
		// akku burn out between 6 and 9 o'clock if we can re-charge it completely by day
		int burnout_time = now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8;
		int burnout_possible = sensors->tin < 18.0 && gstate->soc > 150;
		if (burnout_time && burnout_possible && AKKU_BURNOUT)
			gstate->flags |= FLAG_BURNOUT; // burnout
		else
			gstate->flags |= FLAG_OFFLINE; // offline
	}

	// grid upload in last 3 minutes
	int gu2 = m0->grid < -50 && m1->grid < -50 && m2->grid < -50;
	int gu1 = m0->grid < -75 && m1->grid < -75;
	int gu0 = m0->grid < -100;
	if (gu2 || gu1 || gu0) {
		gstate->flags |= FLAG_GRID_ULOAD;
		xdebug("SOLAR set FLAG_GRID_ULOAD last 3=%d 2=%d 1=%d", m2->grid, m1->grid, m0->grid);
	}

	// grid download in last 3 minutes
	int gd2 = m0->grid > 50 && m1->grid > 50 && m2->grid > 50;
	int gd1 = m0->grid > 75 && m1->grid > 75;
	int gd0 = m0->grid > 100;
	if (gd2 || gd1 || gd0) {
		gstate->flags |= FLAG_GRID_DLOAD;
		xdebug("SOLAR set FLAG_GRID_DLOAD last 3=%d 2=%d 1=%d", m2->grid, m1->grid, m0->grid);
	}

	// akku discharge in last 3 minutes
	int a2 = m0->batt > 50 && m1->batt > 50 && m2->batt > 50;
	int a1 = m0->batt > 75 && m1->batt > 75;
	int a0 = m0->batt > 100;
	if (a2 || a1 || a0) {
		gstate->flags |= FLAG_AKKU_DCHARGE;
		xdebug("SOLAR set FLAG_AKKU_DCHARGE last 3=%d 2=%d 1=%d", m2->batt, m1->batt, m0->batt);
	}

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

	// akku usable energy and estimated time to live based on last 3 minutes average akku discharge or load
	int min = akku_get_min_soc();
	int akku = AKKU_AVAILABLE;
	int batt = (m0->batt + m1->batt + m2->batt) / 3;
	int load = (m0->load + m1->load + m2->load) / 3;
	int bl = batt > load ? batt : load;
	gstate->ttl = bl && gstate->soc > min ? akku * 60 / bl : 0; // in minutes

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
	int critical = gstate->survive < SURVIVE;
	int weekend = (now->tm_wday == 5 || now->tm_wday == 6) && gstate->soc < 500 && !SUMMER; // Friday+Saturday: akku has to be at least 50%
	int soc6 = GSTATE_HOUR(6)->soc;
	int time_window = now->tm_hour >= 9 && now->tm_hour < 16; // between 9 and 16 o'clock
	if (empty || critical || weekend || WINTER)
		// empty / critical / weekend / winter --> always at any time
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
	memcpy(GSTATE_MIN_NOW, gstate, sizeof(gstate_t));
	if (HOURLY)
		memcpy(GSTATE_HOUR_NOW, gstate, sizeof(gstate_t));

	print_gstate();
}

static void calculate_pstate() {

	// lock while calculating new values
	pthread_mutex_lock(&collector_lock);

	// calculate average values over last AVERAGE seconds
	// pv     -> suppress mppt tracking
	// grid   -> suppress meter latency
	// others -> suppress spikes
	pstate_avg = PSTATE_AVG;
	int sec = now->tm_sec > 0 ? now->tm_sec - 1 : 59; // current second is not yet written
	aggregate_rows(pstate_avg, pstate_seconds, PSTATE_SIZE, 60, sec, AVERAGE);
	// dump_array(pstate_avg, PSTATE_SIZE, "[ØØ]", 0);
	// grid should always be around 0 - limit average grid to actual grid
	if (pstate->grid > 0)
		HICUT(pstate_avg->grid, pstate->grid)
	if (pstate->grid < 0)
		LOCUT(pstate_avg->grid, pstate->grid)
	// pv should always be constantly high - set low limit to actual pv to suppress short mppt tracking down spikes
	LOCUT(pstate_avg->pv, pstate->pv)

	// inverter status
	int inv1, inv2;
	inverter_status(&inv1, &inv2);
	pstate->inv = inv1 * 10 + inv2;
	if (!inv1)
		pstate->ac1 = pstate->dc1 = pstate->mppt1 = pstate->mppt2 = pstate->batt = 0;
	if (!inv2)
		pstate->ac2 = pstate->dc2 = pstate->mppt3 = pstate->mppt4 = 0;

	// update self counter
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

	// dissipation
	int diss1 = pstate->dc1 - pstate->ac1;
	int diss2 = pstate->dc2 - pstate->ac2;
	pstate->diss = diss1 + diss2;
	// xdebug("SOLAR Inverter Dissipation diss1=%d diss2=%d adiss=%d", diss1, diss2, pstate->adiss);

	// pv
	ZSHAPE(pstate->mppt1, NOISE)
	ZSHAPE(pstate->mppt2, NOISE)
	ZSHAPE(pstate->mppt3, NOISE)
	ZSHAPE(pstate->mppt4, NOISE)
	pstate->pv = pstate->mppt1 + pstate->mppt2 + pstate->mppt3 + pstate->mppt4;
	ZDELTA(pstate->dpv, pstate->pv, s1->pv, DELTA)

	// grid
	ZDELTA(pstate->dgrid, pstate->grid, s1->grid, DELTA)

	// load - meter latency is 1-2s - check nightly akku service interval -> should be nearly no load change when akku goes off
	pstate->load = pstate->grid + (s1->ac1 + s2->ac1) / 2 + (s1->ac2 + s2->ac2) / 2;

	// ratio pv / load - only when we have pv and load
	pstate->pload = pstate_avg->pv && pstate_avg->load ? (pstate_avg->pv - pstate_avg->diss) * 100 / pstate_avg->load : 0;
	LOCUT(pstate->pload, 0)

	// clear flags
	// TODO delta überhaupt noch nötig?
	pstate->flags = 0;

	// check if we have delta ac power anywhere
	if (abs(pstate->grid - s1->grid) > DELTA)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac1 - s1->ac1) > DELTA)
		pstate->flags |= FLAG_DELTA;
	if (abs(pstate->ac2 - s1->ac2) > DELTA)
		pstate->flags |= FLAG_DELTA;

	// skip further calculations when offline
	if (GSTATE_OFFLINE)

		pstate->ramp = 0;

	else {
		// online

		// more history states
		pstate_t *s5 = PSTATE_SEC_LAST5;
		pstate_t *s10 = PSTATE_SEC_LAST10;

		// emergency shutdown: grid download at all states or akku discharge at one of them
		int egrid = pstate->grid > EMERGENCY && s1->grid > EMERGENCY && s2->grid > EMERGENCY && s3->grid > EMERGENCY;
		int eakku = pstate->batt > EMERGENCY || s5->batt > EMERGENCY || s10->batt > EMERGENCY;
		if ((egrid || eakku) && pstate->pload < 100) {
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
		if (psum < pstate->grid - MINIMUM || psum > pstate->grid + MINIMUM) {
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
		if (pstate->grid < -NOISE && pstate->batt > NOISE) {
			int waste = abs(pstate->grid) < pstate->batt ? abs(pstate->grid) : pstate->batt;
			xlog("SOLAR wasting power %d akku -> grid", waste);
			pstate->flags &= ~FLAG_VALID;
		}
		if (pstate->load <= 0) {
			xlog("SOLAR zero/negative load detected %d", pstate->load);
			pstate->flags &= ~FLAG_VALID;
		}
		if (inv1 != I_STATUS_MPPT) {
			xlog("SOLAR Inverter1 state %d expected %d", inv1, I_STATUS_MPPT);
			pstate->flags &= ~FLAG_VALID;
		}
		if (inv2 != I_STATUS_MPPT) {
			// xlog("SOLAR Inverter2 state %d expected %d ", inv2, I_STATUS_MPPT);
			pstate->flags &= ~FLAG_VALID;
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

		// state is stable when we have no grid load
		if (-NOISE < pstate_avg->grid && pstate_avg->grid < NOISE)
			pstate->flags |= FLAG_STABLE;

		// load is completely satisfied from secondary inverter
		if ((-NOISE < pstate_avg->ac1 && pstate_avg->ac1 < NOISE) || pstate_avg->load < pstate_avg->ac2)
			pstate->flags |= FLAG_EXTRAPOWER;

		// TODO mppt tracking erkennen und ramping verhindern, zuerst geht ac runter, dann dc und mpptx, 1-2 sekunden später grid rückmeldung
		// und eine sekunde später alles wieder rauf

		// TODO wolken: es müssen beide ac / dc values gemeinsam runter gehen -> sofort delta ramp runter wenn pv < load

		// TODO maximalen surplus basierend auf pvmin/pvmax/pvavg setzen z.B. wenn delta pvmin/pvmax zu groß -> ersetzt DISTORTION logik

		// TODO oder delta ac verwenden?

		// calculate ramp power - here we use average values
		if (time(NULL) % 10 == 0) {

			// absolute average grid driven slow ramp every 10 seconds
			pstate->ramp = pstate_avg->grid * -1;

			// suppress when below 0 as long as we have enough
			if (pstate->pload > 110)
				LOCUT(pstate->ramp, 0)

			// suppress when above 0 as long as not enough
			if (pstate->pload < 100)
				HICUT(pstate->ramp, 0)

			// nearly equal - max. one step up
			if (100 <= pstate->pload && pstate->pload <= 110)
				HICUT(pstate->ramp, RAMP)

			// slowly push grid down
			if (NOISE < pstate_avg->grid && pstate_avg->grid < RAMP * 2)
				pstate->ramp = -RAMP;

			// always minus akku when discharging
			if (pstate_avg->batt > NOISE)
				pstate->ramp -= pstate_avg->batt;

			// 10% more when falling
			if (PSTATE_PV_FALLING)
				pstate->ramp += pstate->ramp / 10;

			// zero from -RAMP..RAMP
			ZSHAPE(pstate->ramp, RAMP)
			if (pstate->ramp)
#define AVERAGE_GRID_RAMP "SOLAR average grid ramp agrid=%d grid=%d abatt=%d batt=%d pload=%d --> ramp=%d"
				xlog(AVERAGE_GRID_RAMP, pstate_avg->grid, pstate->grid, pstate_avg->batt, pstate->batt, pstate->pload, pstate->ramp);

		} else {

			// relative pv delta driven fast ramp every second
			pstate->ramp = pstate->dpv;

			// suppress when below 0 as long as we have enough
			if (pstate->pload > 110)
				LOCUT(pstate->ramp, 0)

			// suppress when above 0 as long as not enough
			if (pstate->pload < 100)
				HICUT(pstate->ramp, 0)

			// 10% more when falling
			if (PSTATE_PV_FALLING)
				pstate->ramp += pstate->ramp / 10;

			// no ramp up on grid download / akku discharge
			int suppress = pstate_avg->grid > NOISE || pstate->grid > NOISE || pstate_avg->batt > NOISE || pstate->batt > NOISE;
			if (pstate->ramp > NOISE && suppress)
				pstate->ramp = 0;

			// zero from -RAMP..RAMP
			ZSHAPE(pstate->ramp, RAMP)
			if (pstate->ramp)
#define DELTA_PV_RAMP "SOLAR delta pv ramp dpv=%d agrid=%d grid=%d abatt=%d batt=%d pload=%d --> ramp=%d"
				xlog(DELTA_PV_RAMP, pstate->dpv, pstate_avg->grid, pstate->grid, pstate_avg->batt, pstate->batt, pstate->pload, pstate->ramp);
		}
	}

	pthread_mutex_unlock(&collector_lock);

	// copy to history
	memcpy(PSTATE_SEC_NOW, pstate, sizeof(pstate_t));

	// print pstate once per minute / when delta / on grid load
	if (MINLY || PSTATE_DELTA || pstate_avg->grid > NOISE || pstate->ramp)
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
	collect_average_247();

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
	int aloads[24], abatts[24];
	for (int h = 0; h < 24; h++) {
		pstate_t *p = PSTATE_AVG_247(h);
		aloads[h] = p->load;
		abatts[h] = p->batt;
	}
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

	if (DAILY) {
		// aggregate 24 pstate hours into one day
		pstate_t pda, pdc;
		aggregate(&pda, PSTATE_YDAY, PSTATE_SIZE, 24);
		cumulate(&pdc, PSTATE_YDAY, PSTATE_SIZE, 24);
		dump_table(PSTATE_YDAY, PSTATE_SIZE, 24, -1, "SOLAR pstate_hours", PSTATE_HEADER);
		dump_array(&pda, PSTATE_SIZE, "[ØØ]", 0);
		dump_array(&pdc, PSTATE_SIZE, "[++]", 0);
		// aggregate 24 gstate hours into one day
		gstate_t gda, gdc;
		aggregate(&gda, GSTATE_YDAY, GSTATE_SIZE, 24);
		cumulate(&gdc, GSTATE_YDAY, GSTATE_SIZE, 24);
		dump_table(GSTATE_YDAY, GSTATE_SIZE, 24, -1, "SOLAR gstate_hours", GSTATE_HEADER);
		dump_array(&gda, GSTATE_SIZE, "[ØØ]", 0);
		dump_array(&gdc, GSTATE_SIZE, "[++]", 0);
	}

	// aggregate 60 minutes into one hour
	if (HOURLY) {
		aggregate(PSTATE_HOUR_NOW, pstate_minutes, PSTATE_SIZE, 60);
		// dump_table(pstate_minutes, PSTATE_SIZE, 60, -1, "SOLAR pstate_minutes", PSTATE_HEADER);
		// dump_array(PSTATE_HOUR_NOW, PSTATE_SIZE, "[ØØ]", 0);
	}

	// aggregate 60 seconds into one minute
	if (MINLY) {
		aggregate(PSTATE_MIN_NOW, pstate_seconds, PSTATE_SIZE, 60);
		// dump_table(pstate_seconds, PSTATE_SIZE, 60, -1, "SOLAR pstate_seconds", PSTATE_HEADER);
		// dump_array(PSTATE_MIN_NOW, PSTATE_SIZE, "[ØØ]", 0);
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

		// create/append csv files BEFORE(!) calculation
		gnuplot();

		// calculate power state
		calculate_pstate();

		// cron jobs
		if (MINLY)
			minly();
		if (HOURLY)
			hourly();
		if (DAILY)
			daily();

		// web output
		create_pstate_json();
		create_gstate_json();
		create_powerflow_json();

		// PROFILING_LOG(" collector main loop")

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

	// clear pstate
	ZERO(pstate_current);

	load_state();
	mosmix_load_state(now);
	mosmix_load(now, WORK SLASH MARIENBERG, 0);
	collect_average_247();

	return 0;
}

static void stop() {
	// saving state - this is the most important !!!
	store_state();
	mosmix_store_state();

	pthread_mutex_destroy(&collector_lock);
}

MCP_REGISTER(solar_collector, 11, &init, &stop, &loop);
