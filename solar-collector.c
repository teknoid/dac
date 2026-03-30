#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include <sys/time.h>

#include "solar-common.h"
#include "sensors.h"
#include "sunspec.h"
#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

#define COUNTER_METER

#define AKKU_BURNOUT			1

#define DELTAS					20
#define DELTAM					50
#define RAMP					25
#define SUSPICIOUS				500
#define SPIKE					500
#define EMERGENCY				1000
#define EMERGENCY2X				2000

#define PVARIANCE				5
#define PSLOPE					10
#define PSTATE_SPREAD			50

#define GVARIANCE				20
#define GSLOPE					50
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
#define STATS_CSV				"statistics.csv"

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
#define STATS_NOW				(&stats_minutes[now->tm_min])

// stable/unstable
#define PSTATE_3S_STABLE		( (PSTATE_SEC_LAST1->flags & FLAG_STABLE) &&  (PSTATE_SEC_LAST2->flags & FLAG_STABLE) &&  (PSTATE_SEC_LAST3->flags & FLAG_STABLE))
#define PSTATE_3S_UNSTABLE		(!(PSTATE_SEC_LAST1->flags & FLAG_STABLE) && !(PSTATE_SEC_LAST2->flags & FLAG_STABLE) && !(PSTATE_SEC_LAST3->flags & FLAG_STABLE))
#define GSTATE_3M_STABLE		( (GSTATE_MIN_LAST1->flags & FLAG_STABLE) &&  (GSTATE_MIN_LAST2->flags & FLAG_STABLE) &&  (GSTATE_MIN_LAST3->flags & FLAG_STABLE))
#define GSTATE_3M_UNSTABLE		(!(GSTATE_MIN_LAST1->flags & FLAG_STABLE) && !(GSTATE_MIN_LAST2->flags & FLAG_STABLE) && !(GSTATE_MIN_LAST3->flags & FLAG_STABLE))

static struct tm now_tm, *now = &now_tm;

// local params/counter/gstate/pstate/statistics memory
static params_t params_current;
static counter_t counter_hours[HISTORY_SIZE];
static gstate_t gstate_hours[HISTORY_SIZE], gstate_minutes[60], gstate_current;
static pstate_t pstate_hours[HISTORY_SIZE], pstate_minutes[60], pstate_seconds[60], pstate_average_247[24], pstates[32];
static stats_t stats_minutes[60];

// local statistics memory per 10s 1m 10m 1h,  delta, slope and variance pointer
static pstate_t *avgss = &pstates[1], *minss = &pstates[2], *maxss = &pstates[3], *spreadss = &pstates[4];
static pstate_t *minm = &pstates[5], *maxm = &pstates[6], *spreadm = &pstates[7];
static pstate_t *avgmm = &pstates[8], *minmm = &pstates[9], *maxmm = &pstates[10], *spreadmm = &pstates[11];
static pstate_t *minh = &pstates[12], *maxh = &pstates[13], *spreadh = &pstates[14];
static pstate_t *delta = &pstates[15], *deltac = &pstates[16], *deltas = &pstates[17], *deltam = &pstates[18], *deltamm = &pstates[19];
static pstate_t *slos = &pstates[20], *vars = &pstates[21], *slom = &pstates[22], *varm = &pstates[23], *slomm = &pstates[24], *varmm = &pstates[25];

// local semaphores memory
static sequential_t sequential;

// global counter/gstate/pstate/params/semaphore pointer
counter_t counter[10];
gstate_t *gstate = &gstate_current;
pstate_t *pstate = &pstates[0];
params_t *params = &params_current;

// global semaphore pointer
sequential_t *sq = &sequential;

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
		if (!offset || access(RUN SLASH STATS_CSV, F_OK))
			store_csv_header(STATS_HEADER, RUN SLASH STATS_CSV);
		append_table_csv(gstate_minutes, GSTATE_SIZE, 60, offset, RUN SLASH GSTATE_M_CSV);
		append_table_csv(pstate_minutes, PSTATE_SIZE, 60, offset, RUN SLASH PSTATE_M_CSV);
		append_table_csv(stats_minutes, STATS_SIZE, 60, offset, RUN SLASH STATS_CSV);
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
		xlogl_int(line, "Avail", gstate->available);
		xlogl_int(line, "Need", gstate->needed);
		xlogl_percent10(line, "Surv", gstate->survive);
	} else {
		xlogl_int(line, "PV", PSTATE_MIN_NOW->pv);
		xlogl_int(line, "PVMin", minmm->pv);
		xlogl_int(line, "PVAvg", avgmm->pv);
		xlogl_int(line, "PVMax", maxmm->pv);
		xlogl_int_b(line, "∑PV", gstate->pv);
		xlogl_int(line, "Today", gstate->today);
		xlogl_int(line, "Tomo", gstate->tomorrow);
		xlogl_int(line, "SoD", gstate->sod);
		xlogl_int(line, "EoD", gstate->eod);
		xlogl_percent10(line, "Succ", gstate->success);
	}
	snprintf(value, 10, GSTATE_3M_STABLE ? " -" : GSTATE_3M_UNSTABLE ? " ~" : "  ");
	strcat(line, value);
	xlogl_end(line, strlen(line), 0);

	// TODO debugging; remark: last LOAD5/PV5 points to LOAD1/PV1 avg-5 seconds!
//#define GT " now=%3d min=%3d avg=%3d max=%3d spread=%3d last=%3d slope=%3d var=%3d deltac=%3d deltas=%3d"
//	if (!GSTATE_OFFLINE) {
//		xlog("GSTATE PV1" GT, pstate->pv, minm->pv, PSTATE_MIN_NOW->pv, maxm->pv, spreadm->pv, PSTATE_MIN_LAST1->pv, slom->pv, varm->pv, deltac->pv, deltas->pv);
//		xlog("GSTATE PV5" GT, pstate->pv, minmm->pv, avgmm->pv, maxmm->pv, spreadmm->pv, PSTATE_MIN_LAST5->pv, slomm->pv, varmm->pv, deltac->pv, deltas->pv);
//	}
//	xlog("GSTATE LOAD1" GT, pstate->load, minm->load, PSTATE_MIN_NOW->load, maxm->load, spreadm->load, PSTATE_MIN_LAST1->load, slom->load, varm->load, deltac->load, deltas->load);
//	xlog("GSTATE LOAD5" GT, pstate->load, minmm->load, avgmm->load, maxmm->load, spreadmm->load, PSTATE_MIN_LAST5->load, slomm->load, varmm->load, deltac->load, deltas->load);
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
		snprintf(value, 10, PSTATE_3S_STABLE ? " -" : PSTATE_3S_UNSTABLE ? " ~" : "  ");
		strcat(line, value);
	}
	xlogl_end(line, strlen(line), 0);
}

// take over minimum, maximum, average
static void calculate_statistics() {
	stats_t *stats = STATS_NOW;
	stats->pv = round10(PSTATE_MIN_NOW->pv);
	stats->pvmin = round10(minm->pv);
	stats->pvmax = round10(maxm->pv);
	stats->grid = round10(PSTATE_MIN_NOW->grid);
	stats->gridmin = round10(minm->grid);
	stats->gridmax = round10(maxm->grid);
	stats->load = round10(PSTATE_MIN_NOW->load);
	stats->loadmin = round10(minm->load);
	stats->loadmax = round10(maxm->load);
}

static void calculate_counter() {

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
}

static void calculate_gstate_offline() {
	// akku burn out between 6 and 9 o'clock if we can re-charge it completely by day
	// TODO start stunde berechnen damit leer wenn pv startet
	int burnout_recover = gstate->today > params->akku_capacity * 2;
	int burnout_time = now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8;
	int burnout_temp = sensors->tin < 20.0;
	int burnout_soc = gstate->soc > 150;
	if (AKKU_BURNOUT && burnout_recover && burnout_time && burnout_temp && burnout_soc)
		gstate->flags |= FLAG_BURNOUT;
	else
		gstate->flags |= FLAG_OFFLINE;

	// check if grid and load are stable
	int gstable = deltac->grid < DCSTABLE || deltas->grid < DSSTABLE || spreadm->grid < GSTATE_SPREAD;
	int lstable = deltac->load < DCSTABLE || deltas->load < DSSTABLE || spreadm->load < GSTATE_SPREAD;
	if (gstable && lstable) {
		gstate->flags |= FLAG_STABLE;
		xdebug("SOLAR set gstate FLAG_STABLE");
	}
	if (GSTATE_3M_STABLE)
		gstate->flags |= FLAG_STABLE_3M;

	// akku discharge limit
	if (gstate->survive > SURVIVE110 && GSTATE_3M_STABLE) {
		// survive and stable over three minutes - no limit
		params->akku_dlimit = 0;

	} else if (gstate->survive > SURVIVE110 && GSTATE_3M_UNSTABLE) {

		// survive but unstable over three minutes - suppressing single events e.g. refrigerator start
		int adjust = minm->grid - minm->grid / 10;
		ZSHAPE(adjust, RAMP)
		// initially set limit to average load, otherwise push grid uploads to grid downloads by lowering discharge rate
		int dlimit = round10(params->akku_dlimit ? params->akku_dlimit + adjust : PSTATE_MIN_NOW->load);
		LOCUT(dlimit, params->baseload)
		int delta = maxm->grid - minm->grid;
		xlog("SOLAR unstable   grid min=%d max=%d delta=%d   dlimit now=%d adjust=%d new=%d", minm->grid, maxm->grid, delta, params->akku_dlimit, adjust, dlimit);
		params->akku_dlimit = dlimit;

	} else if (gstate->survive < SURVIVE100) {

		// not survive - stretch akku ttl to maximum
		int dlimit = gstate->available && gstate->minutes ? gstate->available * 60 / gstate->minutes / 10 * 10 : 0;
		LOCUT(dlimit, params->baseload)
		// take over initial limit or when delta 20+
		int diff = dlimit > params->akku_dlimit ? (dlimit - params->akku_dlimit) : (params->akku_dlimit - dlimit);
		if (!params->akku_dlimit || diff >= 20) {
			xlog("SOLAR dlimit now=%d new=%d diff=%d baseload=%d", params->akku_dlimit, dlimit, diff, params->baseload);
			params->akku_dlimit = dlimit;
		}
	}

	if (params->akku_dlimit_override)
		params->akku_dlimit = params->akku_dlimit_override; // override
	if (GSTATE_BURNOUT)
		params->akku_dlimit = 0; // burnout
}

static void calculate_gstate_online() {
	// tendency: falling or rising or stable
	// xlog("SOLAR gstate falling or rising: deltam=%d varm=%d slom=%d", delta->pv, varm->pv, slom->pv);
	int pvfall = deltam->pv < -500 || varm->pv < -GVARIANCE || slom->pv < -GSLOPE;
	int pvrise = deltam->pv > 500 || varm->pv > GVARIANCE || slom->pv > GSLOPE;
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
	int soc6 = GSTATE_HOUR(6)->soc;
	int empty = gstate->soc < 100;
	int critical = gstate->survive < SURVIVE150;
	int low = gstate->today < params->akku_capacity || gstate->tomorrow < params->akku_capacity; // today/tomorrow low pv expected
	int weekend = (now->tm_wday == 5 || now->tm_wday == 6) && gstate->soc < 500 && !SUMMER; // Friday+Saturday: akku has to be at least 50%
	int time_window = now->tm_hour >= 9 && now->tm_hour < 15; // between 9 and 15 o'clock
	if (WINTER || empty || critical || low || weekend)
		// winter / empty / critical / low / weekend --> always at any time
		gstate->flags |= FLAG_CHARGE_AKKU;
	else if (SUMMER) {
		// summer: when below 22%
		if (time_window && soc6 < 222)
			gstate->flags |= FLAG_CHARGE_AKKU;
	} else {
		// autumn/spring: when below 33%
		if (time_window && soc6 < 333)
			gstate->flags |= FLAG_CHARGE_AKKU;
	}
	xdebug("SOLAR charge akku soc6=%d empty=%d critical=%d low=%d weekend=%d time=%d", soc6, empty, critical, low, weekend, time_window);

	// akku charge limit
	params->akku_climit = 0;
	if (GSTATE_SUMMER || gstate->today > params->akku_capacity * 2)
		params->akku_climit = params->akku_cmax / 2;
	if (GSTATE_SUMMER || gstate->today > params->akku_capacity * 3)
		params->akku_climit = params->akku_cmax / 3;
	if (GSTATE_SUMMER || gstate->today > params->akku_capacity * 4)
		params->akku_climit = params->akku_cmax / 4;
	if (900 < gstate->soc && gstate->soc < 999)
		params->akku_climit = 1000; // charging slow between 90 and 100%
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

	// akku discharge / grid download / grid upload - always check average against actual
	if (PSTATE_MIN_NOW->akku > params->baseload || (avgmm->akku > RAMP && PSTATE_MIN_NOW->akku > 0))
		gstate->flags |= FLAG_AKKU_DCHARGE;
	if (PSTATE_MIN_NOW->grid > params->baseload || (avgmm->grid > RAMP && PSTATE_MIN_NOW->grid > 0))
		gstate->flags |= FLAG_GRID_DLOAD;
	if (PSTATE_MIN_NOW->grid < RAMP * -4 || (avgmm->grid < RAMP * -2 && PSTATE_MIN_NOW->grid < 0))
		gstate->flags |= FLAG_GRID_ULOAD;

	// akku usable energy and estimated time to live based on 5min average load or akku discharge
	gstate->available = round10(AKKU_AVAILABLE);
	int msoc = akku_get_min_soc();
	int al = avgmm->akku > avgmm->load ? avgmm->akku : avgmm->load;
	gstate->ttl = al && gstate->soc > msoc ? gstate->available * 60 / al : 0; // in minutes

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
	int tocharge = gstate->needed - gstate->available;
	LOCUT(tocharge, 0)
	int available = pstate->pv > 0 ? gstate->eod - tocharge : 0;
	LOCUT(available, 0)
	gstate->survive = gstate->needed ? (available + gstate->available) * 1000 / gstate->needed : 2000;
	HICUT(gstate->survive, 2000)
#define TEMPLATE_SURVIVE "SOLAR survive eod=%d tocharge=%d avail=%d akku=%d need=%d minutes=%d --> %.1f%%"
	xdebug(TEMPLATE_SURVIVE, gstate->eod, tocharge, available, gstate->available, gstate->needed, gstate->minutes, FLOAT10(gstate->survive));

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
		HICUT(pstate->ramp, -RAMP)
		xdebug("SOLAR akku discharge ramp aakku=%d akku=%d ramp=%d", avgss->akku, pstate->akku, pstate->ramp);
		return;
	}

	// always ramp down on grid download
	if (PSTATE_GRID_DLOAD) {
		pstate->ramp = pstate->grid * -1;
		HICUT(pstate->ramp, -RAMP)
		xdebug("SOLAR grid download ramp agrid=%d grid=%d ramp=%d", avgss->grid, pstate->grid, pstate->ramp);
		return;
	}

	// look ahead down ramp on falling pv or grid download
	if ((100 <= avgss->rsl && avgss->rsl <= 105) && (PSTATE_PVFALL || pstate->grid > NOISE5))
		pstate->ramp = -RAMP;

	// akku is passive - keep a little bit grid upload
	if (!pstate->akku && avgss->rsl < 100 && avgss->grid > 0)
		pstate->ramp = -RAMP;
	if (!pstate->akku && avgss->rsl > 105 && avgss->grid < RAMP * -4)
		pstate->ramp = RAMP;

	// akku is active - regulate around 0
	if (pstate->akku && avgss->rsl < 100 && avgss->grid > RAMP * 2)
		pstate->ramp = -RAMP;
	if (pstate->akku && avgss->rsl > 105 && avgss->grid < RAMP * -2)
		pstate->ramp = RAMP;

	// coarse absolute ramp below 90 or above 110
	if (avgss->rsl < 90 || avgss->rsl > 110) {
		if (avgss->grid > 0)
			pstate->ramp = PSTATE_PVFALL ? (avgss->grid * -2) : (avgss->grid * -1); // grid download - double down when pv falling
		else if (avgss->grid < 0)
			pstate->ramp = PSTATE_STABLE_3S ? (avgss->grid / -1) : (avgss->grid / -2); // grid upload - half when unstable last 3 seconds
	}

	// shape
	ZSHAPE(pstate->ramp, RAMP)

	// suppress ramp up
	if (pstate->ramp > 0) {
		int less = pstate->grid > RAMP * -2; // too less grid upload
		int dgrid = pstate->grid > 0; // actual grid download
		int over = dstate->cload > avgmm->pv && !GSTATE_GRID_ULOAD; // calculated load above average pv
		if (PSTATE_PVFALL || less || dgrid || over) {
			xlog("SOLAR suppress up ramp=%d fall=%d less=%d dgrid=%d over=%d", pstate->ramp, PSTATE_PVFALL, less, dgrid, over);
			pstate->ramp = 0;
		}
	}

	// suppress ramp down
	if (pstate->ramp < 0) {
		int plenty = avgss->rsl > 200; // plenty surplus
		int ugrid = pstate->grid < -RAMP; // actual grid upload
		int extra = pstate->load < pstate->ac2; // load completely satisfied by secondary inverter
		if (PSTATE_PVRISE || plenty || ugrid || extra) {
			xlog("SOLAR suppress down ramp=%d rise=%d plenty=%d ugrid=%d extra=%d", pstate->ramp, PSTATE_PVRISE, plenty, ugrid, extra);
			pstate->ramp = 0;
		}
	}

	if (pstate->ramp)
		xdebug("SOLAR ramp rsl=%d agrid=%d grid=%d akku=%d ramp=%d", avgss->rsl, avgss->grid, pstate->grid, pstate->akku, pstate->ramp);
}

static void calculate_pstate_online() {
	// akku discharge / grid download / grid upload - always check average against actual
	if (pstate->akku > RAMP * 4 || (avgss->akku > RAMP * 2 && pstate->akku > 0))
		pstate->flags |= FLAG_AKKU_DCHARGE;
	if (pstate->grid > RAMP * 4 || (avgss->grid > RAMP * 2 && pstate->grid > 0))
		pstate->flags |= FLAG_GRID_DLOAD;
	if (pstate->grid < RAMP * -4 || (avgss->grid < RAMP * -2 && pstate->grid < 0))
		pstate->flags |= FLAG_GRID_ULOAD;

	// meter latency / mppt tracking / too fast pv delta / grid spikes / whatever
	// TODO anpassen nach korrigierter Berechnung - s3 values nehmen?
	int sum = pstate->pv + pstate->grid + pstate->akku + pstate->load * -1;
	if (abs(sum) > SUSPICIOUS) {
		xlog("SOLAR suspicious inverter values detected: sum=%d", sum);
		pstate->flags |= FLAG_INVALID;
	}

	int gdiff = pstate->grid - pstate->l1p - pstate->l2p - pstate->l3p;
	if (gdiff < -RAMP || gdiff > RAMP) {
		xlog("SOLAR suspicious meter values detected p1=%d p2=%d p3=%d grid=%d gdiff=%d ", pstate->l1p, pstate->l2p, pstate->l3p, pstate->grid, gdiff);
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

	int waste = pstate->grid < -NOISE10 && pstate->akku > NOISE10; // wasting akku power to grid
	int draw = pstate->grid > NOISE10 && pstate->akku < -NOISE10; // akku draws power from grid
	if (waste || draw) {
		xlog("SOLAR akku is unbalanced grid=%d akku=%d waste=%d draw=%d", pstate->grid, pstate->akku, waste, draw);
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
	// xlog("SOLAR pstate falling or rising: delta=%d vars=%d slos=%d", delta->pv, vars->pv, slos->pv);
	int pvfall = delta->pv < -100 || vars->pv < -PVARIANCE || slos->pv < -PSLOPE;
	int pvrise = delta->pv > 100 || vars->pv > PVARIANCE || slos->pv > PSLOPE;
	if (pvfall) {
		pstate->flags |= FLAG_PVFALL;
		xdebug("SOLAR set pstate FLAG_PVFALL delta=%d vars=%d slos=%d", delta->pv, vars->pv, slos->pv);
	}
	if (!pvfall && pvrise) {
		pstate->flags |= FLAG_PVRISE;
		xdebug("SOLAR set pstate FLAG_PVRISE delta=%d vars=%d slos=%d", delta->pv, vars->pv, slos->pv);
	}

	// acdelta - delta on any ac lines (dc makes no sense, will be true most time)
//	int stable = spreadss->pv < PSTATE_SPREAD && spreadss->grid < PSTATE_SPREAD && spreadss->load < PSTATE_SPREAD;
	int acdelta = delta->l1p || delta->l2p || delta->l3p || delta->ac1 || delta->ac2;
	if (!pvfall && !pvrise && !acdelta) {
		pstate->flags |= FLAG_STABLE;
		xdebug("SOLAR set pstate FLAG_STABLE");
	} else
		xdebug("SOLAR unstable pvfall=%d pvrise=%d acdelta=%d", pvfall, pvrise, acdelta);

	// stable over 3 seconds
	if (PSTATE_3S_STABLE)
		pstate->flags |= FLAG_STABLE_3S;

	// emergency shutdown: average/current grid download or akku discharge
	int egrid = pstate->grid > EMERGENCY2X || avgss->grid > EMERGENCY;
	int eakku = pstate->akku > EMERGENCY2X || avgss->akku > EMERGENCY;
	if (egrid || eakku) {
		pstate->flags |= FLAG_EMERGENCY;
		xlog("SOLAR set FLAG_EMERGENCY egrid=%d eakku=%d", egrid, eakku);
	}
}

static void calculate_pstate() {
	// clear flags and values
	pstate->flags = pstate->ramp = 0;

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

	// surplus is pure inverters ac output, hi-cutted by pv - no akku discharge, lo-cut 0 - no forced akku charging
	pstate->surp = pstate->ac1 + pstate->ac2;
	HICUT(pstate->surp, pstate->pv)
	LOCUT(pstate->surp, 0)

	// load is inverter ac output plus grid
	pstate->load = pstate->ac1 + pstate->ac2 + pstate->grid;

	// shape
	ZSHAPE(pstate->ac1, NOISE5)
	ZSHAPE(pstate->ac2, NOISE5)
	ZSHAPE(pstate->grid, NOISE5)
	ZSHAPE(pstate->load, NOISE5)
	ZSHAPE(pstate->akku, NOISE5)

	// ratio surplus / load - calculate only when load is positive
	if (pstate->load > 0) {
		pstate->rsl = pstate->surp * 100 / pstate->load;
		HICUT(pstate->rsl, 1000)
	} else
		xlog("SOLAR skip rsl calculation load=%d rsl=%d", pstate->load, pstate->rsl);

	// calculate delta, update delta sum, delta count in one loop
	idelta_x(delta, pstate, PSTATE_SEC_LAST1, deltac, deltas, PSTATE_SIZE, DELTAS);

	// calculate slope and variance five seconds ago
	islope(slos, pstate, PSTATE_SEC_LAST5, PSTATE_SIZE, 5);
	ivariance(vars, pstate, PSTATE_SEC_LAST5, PSTATE_SIZE);

	// calculate delta, slope and variance
	if (MINLY) {
		// one minute ago
		idelta(deltam, PSTATE_MIN_NOW, PSTATE_MIN_LAST1, PSTATE_SIZE, DELTAM);
		islope(slom, PSTATE_MIN_NOW, PSTATE_MIN_LAST1, PSTATE_SIZE, 5);
		ivariance(varm, PSTATE_MIN_NOW, PSTATE_MIN_LAST1, PSTATE_SIZE);
		// five minutes ago
		idelta(deltamm, PSTATE_MIN_NOW, PSTATE_MIN_LAST5, PSTATE_SIZE, DELTAM);
		islope(slomm, PSTATE_MIN_NOW, PSTATE_MIN_LAST5, PSTATE_SIZE, 5);
		ivariance(varmm, PSTATE_MIN_NOW, PSTATE_MIN_LAST5, PSTATE_SIZE);
	}

	// calculate online state and ramp power when valid
	if (!GSTATE_OFFLINE) {
		calculate_pstate_online();
		if (!PSTATE_INVALID)
			calculate_pstate_ramp();
	}

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

}

static void minly() {
	// calculate counter and global state
	calculate_counter();
	calculate_gstate();
	calculate_statistics();

	// reset delta sum and delta count
	ZEROP(deltac);
	ZEROP(deltas);
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

		// aggregate last 10 minutes
		int minu = now->tm_min > 0 ? now->tm_min - 1 : 59; // current minute is not yet written
		iaggregate_mams(avgmm, pstate_minutes, minmm, maxmm, spreadmm, PSTATE_SIZE, 60, minu, 10);
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

		// wait for trigger
		sem_wait(&sq->collector);

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

		// MICROSECONDS(" collector")

		// trigger dispatcher thread - calculation done, critical path continues there
		sem_post(&sq->dispatcher);

		// web output - outside critical path
		create_pstate_json();
		create_gstate_json();
		create_powerflow_json();

#ifdef GNUPLOT_MINLY
		if (MINLY)
			system(GNUPLOT_MINLY);
#endif

#ifdef GNUPLOT_HOURLY
		if (HOURLY)
			system(GNUPLOT_HOURLY);
#endif

		// save pstate SVG - outside critical path
		if (DAILY) {
			char command[64], c = '0' + (now->tm_wday > 0 ? now->tm_wday - 1 : 6);
			snprintf(command, 64, "cp -f %s/pstate.svg %s/pstate-%c.svg", RUN, RUN, c);
			system(command);
			xdebug("SOLAR saved pstate SVG: %s", command);
		}

		// PROFILING_LOG(" collector main loop")
	}
}

static int init() {
	// initialize global time structure
	time_t now_ts = time(NULL);
	localtime_r(&now_ts, &now_tm);

	load_state();
	mosmix_load_state(now);
	mosmix_load(now, WORK SLASH MARIENBERG, 0);
	collect_average_247();

	sem_init(&sq->collector, 0, 0);

	return 0;
}

static void stop() {
	// saving state - this is the most important !!!
	store_state();
	mosmix_store_state();

	sem_close(&sq->collector);
}

MCP_REGISTER(solar_collector, 11, &init, &stop, &loop);
