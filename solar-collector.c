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

#define DELTAS					15
#define DELTAM					50
#define RAMP					25
#define SUSPICIOUS				500
#define SPIKE					500
#define EMERGENCY				1000
#define EMERGENCY2X				2000

#define VARIANCE				10
#define PSTATE_SPREAD			50
#define GSTATE_SPREAD			100
#define GSTATE_SPREAD_PV		500
#define DCSTABLE				10
#define DSSTABLE				500

#define GNUPLOT_MINLY			"/usr/bin/gnuplot -p /var/lib/mcp/solar-minly.gp"
#define GNUPLOT_HOURLY			"/usr/bin/gnuplot -p /var/lib/mcp/solar-hourly.gp"

// hexdump -v -e '6 "%10d ""\n"' /var/lib/mcp/solar-counter*.bin
#define COUNTER_H_FILE			"solar-counter-hours.bin"
#define COUNTER_FILE			"solar-counter.bin"

// hexdump -v -e '16 "%6d ""\n"' /var/lib/mcp/solar-gstate*.bin
#define GSTATE_H_FILE			"solar-gstate-hours.bin"
#define GSTATE_M_FILE			"solar-gstate-minutes.bin"
#define GSTATE_FILE				"solar-gstate.bin"

// hexdump -v -e '27 "%6d ""\n"' /var/lib/mcp/solar-pstate*.bin
#define PSTATE_H_FILE			"solar-pstate-hours.bin"
#define PSTATE_M_FILE			"solar-pstate-minutes.bin"
#define PSTATE_S_FILE			"solar-pstate-seconds.bin"
#define PSTATE_FILE				"solar-pstate.bin"

// CSV files for gnuplot
#define GSTATE_T_CSV			"gstate-today.csv"
#define GSTATE_H_CSV			"gstate-hours.csv"
#define GSTATE_M_CSV			"gstate-minutes.csv"
#define PSTATE_H_CSV			"pstate-hours.csv"
#define PSTATE_M_CSV			"pstate-minutes.csv"
#define PSTATE_S_CSV			"pstate-seconds.csv"
#define PSTATE_AVG247_CSV		"pstate-avg-247.csv"

// JSON files for webui
#define PSTATE_AVG_JSON			"pstate-avg.json"
#define PSTATE_JSON				"pstate.json"
#define GSTATE_JSON				"gstate.json"
#define POWERFLOW_JSON			"powerflow.json"

// counter history
#define COUNTER_HOUR_NOW		(&counter_hours[24 * now->tm_wday + now->tm_hour])

// gstate access pointers
#define GSTATE_MIN_NOW			(&gstate_minutes[now->tm_min])
#define GSTATE_MIN_LAST1		(&gstate_minutes[now->tm_min > 0 ? now->tm_min - 1 : 59])
#define GSTATE_MIN_LAST2		(&gstate_minutes[now->tm_min > 1 ? now->tm_min - 2 : (now->tm_min - 2 + 60)])
#define GSTATE_MIN_LAST3		(&gstate_minutes[now->tm_min > 2 ? now->tm_min - 3 : (now->tm_min - 3 + 60)])
#define GSTATE_MIN_LAST5		(&gstate_minutes[now->tm_min > 4 ? now->tm_min - 5 : (now->tm_min - 5 + 60)])
#define GSTATE_HOUR_NOW			(&gstate_hours[24 * now->tm_wday + now->tm_hour])
#define GSTATE_TODAY			(&gstate_hours[24 * now->tm_wday])
#define GSTATE_YDAY				(&gstate_hours[24 * (now->tm_wday > 0 ? now->tm_wday - 1 : 6)])
#define GSTATE_HOUR(h)			(&gstate_hours[24 * now->tm_wday + (h)])
#define GSTATE_DAY_HOUR(d, h)	(&gstate_hours[24 * (d) + (h)])

// pstate access pointers
#define PSTATE_SEC_NOW			(&pstate_seconds[now->tm_sec])
#define PSTATE_SEC_LAST1		(&pstate_seconds[now->tm_sec > 0 ? now->tm_sec - 1 : 59])
#define PSTATE_SEC_LAST2		(&pstate_seconds[now->tm_sec > 1 ? now->tm_sec - 2 : (now->tm_sec - 2 + 60)])
#define PSTATE_SEC_LAST3		(&pstate_seconds[now->tm_sec > 2 ? now->tm_sec - 3 : (now->tm_sec - 3 + 60)])
#define PSTATE_SEC_LAST5		(&pstate_seconds[now->tm_sec > 4 ? now->tm_sec - 5 : (now->tm_sec - 5 + 60)])
#define PSTATE_MIN_NOW			(&pstate_minutes[now->tm_min])
#define PSTATE_MIN_LAST1		(&pstate_minutes[now->tm_min > 0 ? now->tm_min - 1 : 59])
#define PSTATE_MIN_LAST2		(&pstate_minutes[now->tm_min > 1 ? now->tm_min - 2 : (now->tm_min - 2 + 60)])
#define PSTATE_MIN_LAST3		(&pstate_minutes[now->tm_min > 2 ? now->tm_min - 3 : (now->tm_min - 3 + 60)])
#define PSTATE_MIN_LAST5		(&pstate_minutes[now->tm_min > 4 ? now->tm_min - 5 : (now->tm_min - 5 + 60)])
#define PSTATE_MIN(m)			(&pstate_minutes[m])
#define PSTATE_HOUR_NOW			(&pstate_hours[24 * now->tm_wday + now->tm_hour])
#define PSTATE_YDAY				(&pstate_hours[24 * (now->tm_wday > 0 ? now->tm_wday - 1 : 6)])
#define PSTATE_DAY_HOUR(d, h)	(&pstate_hours[24 * (d) + (h)])
#define PSTATE_SEC(s)			(&pstate_seconds[s])
#define PSTATE_AVG_247(h)		(&pstate_average_247[h])

// stable/unstable
#define PSTATE_3S_STABLE		( (PSTATE_SEC_LAST1->flags & FLAG_STABLE) &&  (PSTATE_SEC_LAST2->flags & FLAG_STABLE) &&  (PSTATE_SEC_LAST3->flags & FLAG_STABLE))
#define PSTATE_3S_UNSTABLE		(!(PSTATE_SEC_LAST1->flags & FLAG_STABLE) && !(PSTATE_SEC_LAST2->flags & FLAG_STABLE) && !(PSTATE_SEC_LAST3->flags & FLAG_STABLE))
#define GSTATE_3M_STABLE		( (GSTATE_MIN_LAST1->flags & FLAG_STABLE) &&  (GSTATE_MIN_LAST2->flags & FLAG_STABLE) &&  (GSTATE_MIN_LAST3->flags & FLAG_STABLE))
#define GSTATE_3M_UNSTABLE		(!(GSTATE_MIN_LAST1->flags & FLAG_STABLE) && !(GSTATE_MIN_LAST2->flags & FLAG_STABLE) && !(GSTATE_MIN_LAST3->flags & FLAG_STABLE))

#define AKKU_PASSIVE			(pstate->akku == 0 || pstate->ac1 == 0)

static struct tm now_tm, *now = &now_tm;

// local counter/gstate/pstate/params memory
static counter_t counter_hours[HISTORY_SIZE];
static gstate_t gstate_hours[HISTORY_SIZE], gstate_minutes[60], gstate_current;
static pstate_t pstate_hours[HISTORY_SIZE], pstate_minutes[60], pstate_seconds[60], pstate_average_247[24], pstates[32];
static params_t params_current;

// statistics per 10s 1m 5m 1h,  delta, slope and variance pointer
static pstate_t *avgss = &pstates[1], *minss = &pstates[2], *maxss = &pstates[3], *spreadss = &pstates[4];
static pstate_t *minm = &pstates[5], *maxm = &pstates[6], *spreadm = &pstates[7];
static pstate_t *avgmm = &pstates[8], *minmm = &pstates[9], *maxmm = &pstates[10], *spreadmm = &pstates[11];
static pstate_t *minh = &pstates[12], *maxh = &pstates[13], *spreadh = &pstates[14];
static pstate_t *delta = &pstates[15], *deltac = &pstates[16], *deltas = &pstates[17], *deltam = &pstates[18], *deltamm = &pstates[19];
static pstate_t *slos = &pstates[20], *vars = &pstates[21], *slom = &pstates[22], *varm = &pstates[23], *slomm = &pstates[24], *varmm = &pstates[25];

// global counter/gstate/pstate/params pointer
counter_t counter[10];
gstate_t *gstate = &gstate_current;
pstate_t *pstate = &pstates[0];
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
	load_blob(STATE SLASH PSTATE_FILE, pstates, sizeof(pstates));
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
	store_blob(STATE SLASH PSTATE_FILE, pstates, sizeof(pstates));
}

static void create_pstate_json() {
	store_array_json(avgss, PSTATE_SIZE, PSTATE_HEADER, RUN SLASH PSTATE_AVG_JSON);
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
	fprintf(fp, POWERFLOW_TEMPLATE, FLOAT10(gstate->soc), pstate->akku, pstate->grid, load, pstate->pv);
	fflush(fp);
	fclose(fp);
}

// create csv file for gnuplot
static void create_gnuplot_csv() {
#ifdef GNUPLOT_MINLY
	if (MINLY || access(RUN SLASH PSTATE_S_CSV, F_OK)) {
		// pstate seconds
		int offset = 60 * (now->tm_min > 0 ? now->tm_min - 1 : 59);
		if (!offset || access(RUN SLASH PSTATE_S_CSV, F_OK))
			store_csv_header(PSTATE_HEADER, RUN SLASH PSTATE_S_CSV);
		append_table_csv(pstate_seconds, PSTATE_SIZE, 60, offset, RUN SLASH PSTATE_S_CSV);
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
		// gstate today and week, pstate week
		store_table_csv(GSTATE_TODAY, GSTATE_SIZE, 24, GSTATE_HEADER, RUN SLASH GSTATE_T_CSV);
		store_table_csv(gstate_hours, GSTATE_SIZE, HISTORY_SIZE, GSTATE_HEADER, RUN SLASH GSTATE_H_CSV);
		store_table_csv(pstate_hours, PSTATE_SIZE, HISTORY_SIZE, PSTATE_HEADER, RUN SLASH PSTATE_H_CSV);
	}
#endif
	// TODO sensors plot
}

static void collect_average_247() {
	char line[LINEBUF], value[10];

	ZERO(pstate_average_247);
	for (int h = 0; h < 24; h++) {
		pstate_t *pavg = PSTATE_AVG_247(h);
		for (int d = 0; d < 7; d++)
			iadd(pavg, PSTATE_DAY_HOUR(d, h), PSTATE_SIZE);
		idiv_const(pavg, PSTATE_SIZE, 7);
	}

	// grid validation
	for (int h = 0; h < 24; h++) {
		pstate_t *pavg = PSTATE_AVG_247(h);
		int cgrid = pavg->l1p + pavg->l2p + pavg->l3p;
		if (abs(cgrid - pavg->grid) > 2)
			xlog("SOLAR average 24/7 grid mismatch hour=%02d p1+p2+p3=%d grid=%d", h, cgrid, pavg->grid);
	}

	// load validation
	for (int h = 0; h < 24; h++) {
		pstate_t *pavg = PSTATE_AVG_247(h);
		int cload = pavg->ac1 + pavg->ac2 + pavg->grid;
		if (abs(cload - pavg->load) > 1)
			xlog("SOLAR average 24/7 load mismatch hour=%02d ac1+ac2+grid=%d load=%d", h, cload, pavg->load);
	}

	// pv validation
	for (int h = 0; h < 24; h++) {
		pstate_t *pavg = PSTATE_AVG_247(h);
		int cpv = pavg->mppt1p + pavg->mppt2p + pavg->mppt3p + pavg->mppt4p;
		if (abs(cpv - pavg->pv) > 1)
			xlog("SOLAR average 24/7   pv mismatch hour=%02d mppt1+mppt2+mppt3+mppt4=%d load=%d", h, cpv, pavg->load);
	}

	strcpy(line, "SOLAR average 24/7 load:");
	for (int h = 0; h < 24; h++) {
		snprintf(value, 10, " %4d", PSTATE_AVG_247(h)->load);
		strcat(line, value);
	}
	xlog(line);

	strcpy(line, "SOLAR average 24/7 akku:");
	for (int h = 0; h < 24; h++) {
		snprintf(value, 10, " %4d", PSTATE_AVG_247(h)->akku);
		strcat(line, value);
	}
	xlog(line);

	// calculate base load and minimum
	int load3 = PSTATE_AVG_247(3)->load, load4 = PSTATE_AVG_247(4)->load, load5 = PSTATE_AVG_247(5)->load;
	params->baseload = round10((load3 + load4 + load5) / 3);
	if (params->baseload <= 0)
		params->baseload = BASELOAD;
	params->minimum = params->baseload / 2;
	xlog("SOLAR baseload=%d minimum=%d", params->baseload, params->minimum);

	store_table_csv(pstate_average_247, PSTATE_SIZE, 24, PSTATE_HEADER, RUN SLASH PSTATE_AVG247_CSV);
	append_line_csv(PSTATE_AVG_247(0), PSTATE_SIZE, 24, RUN SLASH PSTATE_AVG247_CSV); // gnuplot workaround: hour 0 = hour 24
}

static void print_gstate() {
	char line[512], value[10]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "GSTATE ");
	xlogl_bits16(line, NULL, gstate->flags);
	xlogl_int_noise(line, NOISE10, 1, "Grid↓", gstate->consumed);
	xlogl_int_noise(line, NOISE10, 0, "Grid↑", gstate->produced);
	xlogl_int(line, "Load", PSTATE_MIN_NOW->load);
	xlogl_float(line, "SoC", FLOAT10(gstate->soc));
	xlogl_float(line, "Ti", sensors->tin);
	xlogl_float(line, "To", sensors->tout);
	if (GSTATE_OFFLINE) {
		xlogl_float(line, "TTL", FLOAT60(gstate->ttl));
		xlogl_int(line, "Akku", gstate->akku);
		xlogl_int(line, "Need", gstate->needed);
		xlogl_percent10(line, "Surv", gstate->survive);
	} else {
		xlogl_int_b(line, "∑PV", gstate->pv);
		xlogl_int(line, "PV", PSTATE_MIN_NOW->pv);
		xlogl_int(line, "Today", gstate->today);
		xlogl_int(line, "Tomo", gstate->tomorrow);
		xlogl_int(line, "SoD", gstate->sod);
		xlogl_int(line, "EoD", gstate->eod);
		xlogl_percent10(line, "Succ", gstate->success);
	}
	if (GSTATE_3M_STABLE)
		snprintf(value, 10, " -");
	else if (GSTATE_3M_UNSTABLE)
		snprintf(value, 10, " ~");
	else
		snprintf(value, 10, "  ");
	strcat(line, value);
	xlogl_end(line, strlen(line), 0);

	// TODO debugging
#define GT " now=%3d min=%3d avg=%3d max=%3d spread=%3d last=%3d slope=%3d var=%3d deltac=%3d deltas=%3d"
	if (!GSTATE_OFFLINE) {
		xlog("GSTATE PV1" GT, pstate->pv, minm->pv, PSTATE_MIN_NOW->pv, maxm->pv, spreadm->pv, PSTATE_MIN_LAST1->pv, slom->pv, varm->pv, deltac->pv, deltas->pv);
		xlog("GSTATE PV5" GT, pstate->pv, minmm->pv, avgmm->pv, maxmm->pv, spreadmm->pv, 0, slomm->pv, varmm->pv, deltac->pv, deltas->pv);
	}
	xlog("GSTATE LOAD1" GT, pstate->load, minm->load, PSTATE_MIN_NOW->load, maxm->load, spreadm->load, PSTATE_MIN_LAST1->load, slom->load, varm->load, deltac->load, deltas->load);
	xlog("GSTATE LOAD5" GT, pstate->load, minmm->load, avgmm->load, maxmm->load, spreadmm->load, 0, slomm->load, varmm->load, deltac->load, deltas->load);
}

static void print_pstate() {
	char line[512], value[10]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "PSTATE ");
	xlogl_bits16(line, NULL, pstate->flags);
	xlogl_int_noise(line, NOISE10, 1, "Grid", pstate->grid);
	xlogl_int_noise(line, NOISE10, 1, "Akku", pstate->akku);
	xlogl_int(line, "Load", pstate->load);
	snprintf(value, 10, " I:%d:%d", inv1->state, inv2->state);
	strcat(line, value);
	if (!GSTATE_OFFLINE) {
		xlogl_int(line, "PV10", pstate->mppt1p + pstate->mppt2p);
		xlogl_int(line, "PV7", pstate->mppt3p + pstate->mppt4p);
		xlogl_int(line, "Surp", pstate->surp);
		xlogl_int(line, "RSL", pstate->rsl);
		xlogl_int_noise(line, NOISE10, 0, "Ramp", pstate->ramp);
	}
	if (PSTATE_3S_STABLE)
		snprintf(value, 10, " -");
	else if (PSTATE_3S_UNSTABLE)
		snprintf(value, 10, " ~");
	else
		snprintf(value, 10, "  ");
	strcat(line, value);
	xlogl_end(line, strlen(line), 0);
}

static void calculate_statistics() {
// take over minimum, maximum, average; limit 10m average to 10s average
//  gstate->gridmin... TODO auslagern in eine mams struktur mit 60*24 einträgen
//	gstate->loadavg = round10(PSTATE_MIN_NOW->load);
//	gstate->loadmin = round10(minm->load);
//	gstate->loadmax = round10(maxm->load);
//	gstate->pvavg = round10(PSTATE_MIN_NOW->pv);
//	gstate->pvmin = round10(minm->pv);
//	gstate->pvmax = round10(maxm->pv);
//  json
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

static void calculate_gstate_offline() {
	// akku burn out between 6 and 9 o'clock if we can re-charge it completely by day
	// TODO start stunde berechnen damit leer wenn pv startet
	int burnout_time = now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8;
	int burnout_possible = sensors->tin < 18.0 && gstate->soc > 150;
	if (burnout_time && burnout_possible && AKKU_BURNOUT)
		gstate->flags |= FLAG_BURNOUT; // burnout
	else
		gstate->flags |= FLAG_OFFLINE; // offline

	// check if grid and load are stable
	int gstable = deltac->grid < DCSTABLE || deltas->grid < DSSTABLE || spreadm->grid < GSTATE_SPREAD;
	int lstable = deltac->load < DCSTABLE || deltas->load < DSSTABLE || spreadm->load < GSTATE_SPREAD;
	if (gstable && lstable) {
		gstate->flags |= FLAG_STABLE;
		xdebug("SOLAR set gstate FLAG_STABLE");
	}

	// akku discharge limit
	if (gstate->survive > SURVIVE110 && GSTATE_3M_STABLE) {
		// survive and stable over three minutes - no limit
		params->akku_dlimit = 0;
	} else if (gstate->survive > SURVIVE110 && GSTATE_3M_UNSTABLE) {
		// survive but unstable over three minutes - suppressing single events e.g. refrigerator start - set limit to average load
		int dlimit = round10(PSTATE_MIN_NOW->load);
		// take over initial limit or falling limits (forcing grid download) or rising limits (lowering grid download)
		int fall = dlimit < params->akku_dlimit && PSTATE_MIN_NOW->grid < params->baseload / 2;
		int rise = dlimit > params->akku_dlimit && PSTATE_MIN_NOW->grid > params->baseload / 2;
		xlog("SOLAR dlimit now=%d new=%d grid=%d", params->akku_dlimit, dlimit, PSTATE_MIN_NOW->grid);
		if (!params->akku_dlimit || fall || rise)
			params->akku_dlimit = dlimit;
		// not below baseload
		LOCUT(params->akku_dlimit, params->baseload)
	} else if (gstate->survive < SURVIVE100) {
		// not survive - stretch akku ttl to maximum
		int dlimit = gstate->akku && gstate->minutes ? gstate->akku * 60 / gstate->minutes / 10 * 10 : 0;
		// take over initial limit or when delta 20+
		int diff = dlimit > params->akku_dlimit ? (dlimit - params->akku_dlimit) : (params->akku_dlimit - dlimit);
		xlog("SOLAR dlimit now=%d new=%d diff=%d", params->akku_dlimit, dlimit, diff);
		if (!params->akku_dlimit || diff >= 20)
			params->akku_dlimit = dlimit;
		// not below baseload
		LOCUT(params->akku_dlimit, params->baseload)
	}
	if (params->akku_dlimit_override)
		params->akku_dlimit = params->akku_dlimit_override; // override
	if (GSTATE_BURNOUT)
		params->akku_dlimit = 0; // burnout
}

static void calculate_gstate_online() {
	// tendency: falling or rising or stable
	int pvfall = deltam->pv < -500 || varm->pv < -VARIANCE;
	int pvrise = deltam->pv > 500 || varm->pv > VARIANCE;
	if (pvfall) {
		gstate->flags |= FLAG_PVFALL;
		xdebug("SOLAR set gstate FLAG_PVFALL");
	}
	if (pvrise) {
		gstate->flags |= FLAG_PVRISE;
		xdebug("SOLAR set gstate FLAG_PVRISE");
	}
	int stable = spreadm->pv < GSTATE_SPREAD_PV;
	if (stable && !pvfall && !pvrise) {
		gstate->flags |= FLAG_STABLE;
		xdebug("SOLAR set gstate FLAG_STABLE");
	}

	// force off when average rsl goes below 95%
	if (PSTATE_MIN_NOW->rsl < 95) {
		gstate->flags |= FLAG_FORCE_OFF;
		xlog("SOLAR set FLAG_FORCE_OFF rsl=%d", PSTATE_MIN_NOW->rsl);
	}

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
	int critical = gstate->survive < SURVIVE150;
	int weekend = (now->tm_wday == 5 || now->tm_wday == 6) && gstate->soc < 500 && !SUMMER; // Friday+Saturday: akku has to be at least 50%
	int soc6 = GSTATE_HOUR(6)->soc;
	int time_window = now->tm_hour >= 9 && now->tm_hour < 15; // between 9 and 15 o'clock
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

	// akku charge limit
	params->akku_climit = 0;
	if (GSTATE_SUMMER || gstate->today > params->akku_capacity * 2)
		params->akku_climit = params->akku_cmax / 2;
	if (GSTATE_SUMMER || gstate->today > params->akku_capacity * 3)
		params->akku_climit = params->akku_cmax / 3;
	if (GSTATE_SUMMER || gstate->today > params->akku_capacity * 4)
		params->akku_climit = params->akku_cmax / 4;
	if (params->akku_climit_override)
		params->akku_climit = params->akku_climit_override;
}

static void calculate_gstate() {
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

	// clear state flags
	gstate->flags = 0;

	// summer / winter mode
	if (SUMMER)
		gstate->flags |= FLAG_SUMMER;

	if (WINTER)
		gstate->flags |= FLAG_WINTER;

	// akku discharge / grid download / grid upload
	if (avgmm->akku > RAMP || PSTATE_MIN_NOW->akku > params->baseload)
		gstate->flags |= FLAG_AKKU_DCHARGE;
	if (avgmm->grid > RAMP || PSTATE_MIN_NOW->grid > params->baseload)
		gstate->flags |= FLAG_GRID_DLOAD;
	if (avgmm->grid < RAMP * -2 || PSTATE_MIN_NOW->grid < RAMP * -4)
		gstate->flags |= FLAG_GRID_ULOAD;

	// akku usable energy and estimated time to live based on 5min average load or akku discharge
	gstate->akku = round10(AKKU_AVAILABLE);
	int msoc = akku_get_min_soc();
	int al = avgmm->akku > avgmm->load ? avgmm->akku : avgmm->load;
	gstate->ttl = al && gstate->soc > msoc ? gstate->akku * 60 / al : 0; // in minutes

	// collect mosmix forecasts
	mosmix_collect(now, &gstate->tomorrow, &gstate->today, &gstate->sod, &gstate->eod);
	gstate->success = gstate->sod > params->minimum && gstate->pv > 0 ? gstate->pv * 1000 / gstate->sod : 0;
	HICUT(gstate->success, 2000)
	xdebug("SOLAR pv=%d sod=%d eod=%d success=%.1f%%", gstate->pv, gstate->sod, gstate->eod, FLOAT10(gstate->success));

	// collect power to survive overnight and discharge rate
	int akkus[24], loads[24];
	for (int h = 0; h < 24; h++) {
		akkus[h] = PSTATE_AVG_247(h)->akku;
		loads[h] = PSTATE_AVG_247(h)->load;
	}
	mosmix_needed(now, params->baseload, &gstate->needed, &gstate->minutes, akkus, loads);
	// take over last value when zero but pv not yet started
	if (pstate->pv < 0 && !gstate->needed)
		gstate->needed = GSTATE_MIN_LAST1->needed;

	// survival factor
	int tocharge = gstate->needed - gstate->akku;
	LOCUT(tocharge, 0)
	int available = pstate->pv > 0 ? gstate->eod - tocharge : 0;
	LOCUT(available, 0)
	gstate->survive = gstate->needed ? (available + gstate->akku) * 1000 / gstate->needed : 2000;
	HICUT(gstate->survive, 2000)
#define TEMPLATE_SURVIVE "SOLAR survive eod=%d tocharge=%d avail=%d akku=%d need=%d minutes=%d --> %.1f%%"
	xdebug(TEMPLATE_SURVIVE, gstate->eod, tocharge, available, gstate->akku, gstate->needed, gstate->minutes, FLOAT10(gstate->survive));

	// offline when 5min average pv goes below minimum
	int offline = avgmm->pv < params->minimum;
	if (offline)
		calculate_gstate_offline();
	else
		calculate_gstate_online();

	// copy to history
	memcpy(GSTATE_MIN_NOW, gstate, sizeof(gstate_t));
	if (HOURLY)
		memcpy(GSTATE_HOUR_NOW, gstate, sizeof(gstate_t));

	print_gstate();
}

static void calculate_pstate_ramp() {
	// always ramp down on akku discharge
	if (PSTATE_AKKU_DCHARGE) {
		pstate->ramp = pstate->akku * -1;
		xdebug("SOLAR akku discharge ramp aakku=%d akku=%d ramp=%d", avgss->akku, pstate->akku, pstate->ramp);
		return;
	}

	// always ramp down on grid download greater than akku charging
	if (PSTATE_GRID_DLOAD && pstate->grid > pstate->akku * -1) {
		pstate->ramp = pstate->grid * -1;
		xdebug("SOLAR grid download ramp agrid=%d grid=%d akku=%d ramp=%d", avgss->grid, pstate->grid, pstate->akku, pstate->ramp);
		return;
	}

	// grid download and rsl below 90 - coarse absolute down ramp
	if (PSTATE_GRID_DLOAD && avgss->rsl < 90) {
		pstate->ramp = avgss->grid * -1;
		ZSHAPE(pstate->ramp, RAMP)
		if (pstate->ramp)
			xdebug("SOLAR average grid down ramp rsl=%d agrid=%d grid=%d ramp=%d", avgss->rsl, avgss->grid, pstate->grid, pstate->ramp);
	}

	// grid upload and rsl above 110 - coarse absolute up ramp
	if (PSTATE_GRID_ULOAD && avgss->rsl > 110) {
		pstate->ramp = PSTATE_3S_STABLE ? (avgss->grid / -1) : (avgss->grid / -2);
		ZSHAPE(pstate->ramp, RAMP)
		if (pstate->ramp)
			xdebug("SOLAR average grid up ramp rsl=%d agrid=%d grid=%d ramp=%d", avgss->rsl, avgss->grid, pstate->grid, pstate->ramp);
	}

	// single step up / down
	if (90 <= avgss->rsl && avgss->rsl <= 110) {
		if (avgss->grid > 5)
			pstate->ramp = -RAMP;
		if (avgss->grid < -RAMP)
			pstate->ramp = RAMP;
		// keep rsl above 100% when akku does not regulate (either full or extra power)
		if (avgss->rsl < 100 && AKKU_PASSIVE)
			pstate->ramp = -RAMP;
		if (pstate->ramp)
			xdebug("SOLAR single step ramp rsl=%d agrid=%d grid=%d ramp=%d", avgss->rsl, avgss->grid, pstate->grid, pstate->ramp);
	}

	// suppress ramp up
	int little = avgss->rsl < 105; // too little surplus
	int dgrid = pstate->grid > 0; // actual grid download
	int waste = pstate->grid < 0 && pstate->akku > 0; // wasting power akku --> grid
	int over = dstate->cload > avgmm->pv && !GSTATE_GRID_ULOAD; // calculated load above 5min average pv
	int suppress_up = PSTATE_PVFALL || little || dgrid || waste || over;
	if (pstate->ramp > 0 && suppress_up) {
		xdebug("SOLAR suppress up ramp=%d fall=%d little=%d dgrid=%d waste=%d over=%d", pstate->ramp, PSTATE_PVFALL, little, dgrid, waste, over);
		pstate->ramp = 0;
	}

	// suppress ramp down
	int plenty = avgss->rsl > 200; // plenty surplus
	int ugrid = pstate->grid < -100; // actual grid upload
	int below = dstate->cload < minmm->surp; // calculated load below 5min minimum surplus
	int extra = pstate->load < pstate->ac2; // load completely satisfied by secondary inverter
	int suppress_down = PSTATE_PVRISE || plenty || ugrid || below || extra;
	if (pstate->ramp < 0 && suppress_down) {
		xdebug("SOLAR suppress down ramp=%d rise=%d plenty=%d ugrid=%d below=%d extra=%d", pstate->ramp, PSTATE_PVRISE, plenty, ugrid, below, extra);
		pstate->ramp = 0;
	}
}

static void calculate_pstate_online() {
	// emergency shutdown: average/current grid download or akku discharge
	int egrid = pstate->grid > EMERGENCY2X || avgss->grid > EMERGENCY;
	int eakku = pstate->akku > EMERGENCY2X || avgss->akku > EMERGENCY;
	if (egrid || eakku) {
		int akku_unregulated = pstate->grid > EMERGENCY && pstate->akku < -EMERGENCY;
		if (akku_unregulated) {
			xlog("SOLAR suppress FLAG_EMERGENCY egrid=%d eakku=%d", egrid, eakku);
		} else if (gstate->soc > 100) {
			// no limit, not below 10%
			params->akku_dlimit = 0;
			pstate->flags |= FLAG_EMERGENCY;
			xlog("SOLAR set FLAG_EMERGENCY egrid=%d eakku=%d", egrid, eakku);
		}
	}

	// meter latency / mppt tracking / too fast pv delta / grid spikes / etc.
	// TODO anpassen nach korrigierter Berechnung - s3 values nehmen?
	int sum = pstate->pv + pstate->grid + pstate->akku + pstate->load * -1;
	if (abs(sum) > SUSPICIOUS) {
		xlog("SOLAR suspicious inverter values detected: sum=%d", sum);
		pstate->flags |= FLAG_INVALID;
	}
	int psum = pstate->l1p + pstate->l2p + pstate->l3p;
	if (psum < pstate->grid - params->minimum || psum > pstate->grid + params->minimum) {
		xlog("SOLAR suspicious meter values detected p1=%d p2=%d p3=%d sum=%d grid=%d", pstate->l1p, pstate->l2p, pstate->l3p, psum, pstate->grid);
		pstate->flags |= FLAG_INVALID;
	}
	if (abs(delta->l1p) > SPIKE || abs(delta->l2p) > SPIKE || abs(delta->l3p) > SPIKE) {
		xlog("SOLAR grid spike detected dgrid=%d dp1=%d dp2=%d dp3=%d", delta->grid, delta->l1p, delta->l2p, delta->l3p);
		pstate->flags |= FLAG_INVALID;
		pstate->grid /= 2; // damping 50%
	}
	if (pstate->load < 0) {
		xlog("SOLAR negative load detected %d", pstate->load);
		pstate->flags |= FLAG_INVALID;
	}
	if (0 <= pstate->load && pstate->load < 0) {
		xlog("SOLAR suspicious small load detected %d", pstate->load);
		pstate->flags |= FLAG_INVALID;
	}
	if (inv1->state != I_STATUS_MPPT) {
		xlog("SOLAR Inverter1 state %d expected %d", inv1->state, I_STATUS_MPPT);
		pstate->flags |= FLAG_INVALID;
	}
	if (inv2->state != I_STATUS_MPPT) {
		// xlog("SOLAR Inverter2 state %d expected %d ", inv2->state, I_STATUS_MPPT);
		// pstate->flags |= FLAG_INVALID;
	}

	// tendency: falling or rising or stable, fall has prio
	int pvfall = delta->pv < -100 || vars->pv < -VARIANCE;
	int pvrise = delta->pv > 100 || vars->pv > VARIANCE;
	if (pvfall) {
		pstate->flags |= FLAG_PVFALL;
		xdebug("SOLAR set pstate FLAG_PVFALL");
	}
	if (!pvfall && pvrise) {
		pstate->flags |= FLAG_PVRISE;
		xdebug("SOLAR set pstate FLAG_PVRISE");
	}
	// acdelta - delta on any ac lines (dc makes no sense, will be true most time)
	int stable = spreadss->pv < PSTATE_SPREAD && spreadss->grid < PSTATE_SPREAD && spreadss->load < PSTATE_SPREAD;
	int acdelta = delta->l1p || delta->l2p || delta->l3p || delta->ac1 || delta->ac2;
	if (stable && !pvfall && !pvrise && !acdelta) {
		pstate->flags |= FLAG_STABLE;
		xdebug("SOLAR set pstate FLAG_STABLE");
	}
}

static void calculate_pstate() {
	// lock while calculating new values
	pthread_mutex_lock(&collector_lock);

	// clear flags and values
	pstate->flags = pstate->surp = pstate->rsl = pstate->ramp = 0;

	// workaround 31.10.2025 10:28:59 SOLAR suspicious meter values detected p1=-745 p2=-466 p3=1211 sum=0 grid=6554
	pstate->grid = pstate->l1p + pstate->l2p + pstate->l3p;

	// update self counter
	if (pstate->grid > 0)
		CS_NOW->consumed += pstate->grid;
	if (pstate->grid < 0)
		CS_NOW->produced += pstate->grid * -1;
	CS_NOW->mppt1 += pstate->mppt1p;
	CS_NOW->mppt2 += pstate->mppt2p;
	CS_NOW->mppt3 += pstate->mppt3p;
	CS_NOW->mppt4 += pstate->mppt4p;

	// pv
	ZSHAPE(pstate->mppt1p, NOISE5)
	ZSHAPE(pstate->mppt2p, NOISE5)
	ZSHAPE(pstate->mppt3p, NOISE5)
	ZSHAPE(pstate->mppt4p, NOISE5)
	pstate->pv = pstate->mppt1p + pstate->mppt2p + pstate->mppt3p + pstate->mppt4p;

	// load is inverter ac output plus grid
	pstate->load = pstate->ac1 + pstate->ac2 + pstate->grid;

	// surplus is pure inverters ac output, hi-cutted by pv - no akku discharge, lo-cut 0 - no forced akku charging
	pstate->surp = pstate->ac1 + pstate->ac2;
	HICUT(pstate->surp, pstate->pv)
	LOCUT(pstate->surp, 0)

	// ratio surplus / load
	pstate->rsl = pstate->load ? pstate->surp * 100 / pstate->load : 0;
	HICUT(pstate->rsl, 1000)

	// calculate delta, update delta sum, delta count in one loop
	idelta_x(delta, pstate, PSTATE_SEC_LAST1, deltac, deltas, PSTATE_SIZE, DELTAS);

	// calculate slope and variance five seconds ago
	islope(slos, pstate, PSTATE_SEC_LAST5, PSTATE_SIZE, 5, NOISE10);
	ivariance(vars, pstate, PSTATE_SEC_LAST5, PSTATE_SIZE);

	// calculate delta, slope and variance
	if (MINLY) {
		// one minute ago
		idelta(deltam, PSTATE_MIN_NOW, PSTATE_MIN_LAST1, PSTATE_SIZE, DELTAM);
		islope(slom, PSTATE_MIN_NOW, PSTATE_MIN_LAST1, PSTATE_SIZE, 5, NOISE10);
		ivariance(varm, PSTATE_MIN_NOW, PSTATE_MIN_LAST1, PSTATE_SIZE);
		// five minutes ago
		idelta(deltamm, PSTATE_MIN_NOW, PSTATE_MIN_LAST5, PSTATE_SIZE, DELTAM);
		islope(slomm, PSTATE_MIN_NOW, PSTATE_MIN_LAST5, PSTATE_SIZE, 5, NOISE10);
		ivariance(varmm, PSTATE_MIN_NOW, PSTATE_MIN_LAST5, PSTATE_SIZE);
	}

	// shape
	ZSHAPE(pstate->ac1, NOISE5)
	ZSHAPE(pstate->ac2, NOISE5)
	ZSHAPE(pstate->akku, NOISE5)
	ZSHAPE(pstate->grid, NOISE5)
	ZSHAPE(pstate->load, NOISE5)

	// akku discharge / grid download / grid upload
	if (avgss->akku > RAMP || pstate->akku > RAMP * 2)
		pstate->flags |= FLAG_AKKU_DCHARGE;
	if (avgss->grid > RAMP || pstate->grid > RAMP * 2)
		pstate->flags |= FLAG_GRID_DLOAD;
	if (avgss->grid < RAMP * -2 || pstate->grid < RAMP * -4)
		pstate->flags |= FLAG_GRID_ULOAD;

	// calculate online state and ramp power when valid
	if (!GSTATE_OFFLINE) {
		calculate_pstate_online();
		if (!PSTATE_INVALID)
			calculate_pstate_ramp();
	}

	// calculations done
	pthread_mutex_unlock(&collector_lock);

	// copy to history
	memcpy(PSTATE_SEC_NOW, pstate, sizeof(pstate_t));

	// print pstate once per minute / when invalid / when delta / on grid load / ramp
	if (MINLY || PSTATE_INVALID || PSTATE_GRID_DLOAD || pstate->ramp)
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

	// collect sod errors and scale all remaining eod values, success factor before and after scaling in succ1/succ2
	int succ1, succ2;
	mosmix_scale(now, &succ1, &succ2);
	gstate->forecast = succ1;
	HICUT(gstate->forecast, 2000)

	// mosmix today and tomorrow
	mosmix_store_csv();

	// reset override limits
	if (now->tm_hour == 6)
		params->akku_climit_override = params->akku_dlimit_override = 0;

#ifdef GNUPLOT_HOURLY
	// paint new diagrams
	system(GNUPLOT_HOURLY);
#endif
}

static void minly() {
	// calculate counter and global state
	calculate_counter();
	calculate_gstate();
	calculate_statistics();

	// reset delta sum and delta count
	ZEROP(deltac);
	ZEROP(deltas);

#ifdef GNUPLOT_MINLY
	// paint new diagrams
	system(GNUPLOT_MINLY);
#endif
}

static void aggregate_state() {
	// reverse order: first aggregate hours, then minutes, then seconds

	if (DAILY) {
		// aggregate 24 pstate hours into one day
		pstate_t pda, pdc;
		iaggregate(&pda, PSTATE_YDAY, PSTATE_SIZE, 24);
		icumulate(&pdc, PSTATE_YDAY, PSTATE_SIZE, 24);
		dump_table(PSTATE_YDAY, PSTATE_SIZE, 24, -1, "SOLAR pstate_hours", PSTATE_HEADER);
		dump_array(&pda, PSTATE_SIZE, "[ØØ]", 0);
		dump_array(&pdc, PSTATE_SIZE, "[++]", 0);
		// aggregate 24 gstate hours into one day
		gstate_t gda, gdc;
		iaggregate(&gda, GSTATE_YDAY, GSTATE_SIZE, 24);
		icumulate(&gdc, GSTATE_YDAY, GSTATE_SIZE, 24);
		dump_table(GSTATE_YDAY, GSTATE_SIZE, 24, -1, "SOLAR gstate_hours", GSTATE_HEADER);
		dump_array(&gda, GSTATE_SIZE, "[ØØ]", 0);
		dump_array(&gdc, GSTATE_SIZE, "[++]", 0);
	}

	if (HOURLY) {
		// aggregate 60 minutes into one hour
		iaggregate_mams(PSTATE_HOUR_NOW, pstate_minutes, minh, maxh, spreadh, PSTATE_SIZE, 60, 0, 60);
		// dump_table(pstate_minutes, PSTATE_SIZE, 60, -1, "SOLAR pstate_minutes", PSTATE_HEADER);
		// dump_array(PSTATE_HOUR_NOW, PSTATE_SIZE, "[ØØ]", 0);
	}

	if (MINLY) {
		// aggregate 60 seconds into one minute
		iaggregate_mams(PSTATE_MIN_NOW, pstate_seconds, minm, maxm, spreadm, PSTATE_SIZE, 60, 0, 60);
		// dump_table(pstate_seconds, PSTATE_SIZE, 60, -1, "SOLAR pstate_seconds", PSTATE_HEADER);
		// dump_array(PSTATE_MIN_NOW, PSTATE_SIZE, "[ØØ]", 0);

		// aggregate last 5 minutes
		int minu = now->tm_min > 0 ? now->tm_min - 1 : 59; // current minute is not yet written
		iaggregate_mams(avgmm, pstate_minutes, minmm, maxmm, spreadmm, PSTATE_SIZE, 60, minu, 5);
	}

	// aggregate last 10 seconds
	int seco = now->tm_sec > 0 ? now->tm_sec - 1 : 59; // current second is not yet calculated
	iaggregate_mams(avgss, pstate_seconds, minss, maxss, spreadss, PSTATE_SIZE, 60, seco, 10);
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

		// create/append gnuplot csv files BEFORE(!) aggregation/calculation
		create_gnuplot_csv();

		// aggregate state values into day-hour-minute when 0-0-0
		aggregate_state();

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
