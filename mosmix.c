// gcc -Wall -DMOSMIX_MAIN -I ./include/ -o mosmix mosmix.c utils.c -lpthread

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include <sys/sysinfo.h>

#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

// !!! never calculate average from errors/percents
// !!! always sum up values and then calculate
// 33	44				75%
// 333	555				60%
// 3333	6666			50%
// --------------------------
// 3699	7265	50,9%	61,6%

// temperature coefficient per mppt, scaled as x100
#define TCOPMAX1				-34
#define TCOPMAX2				-30
#define TCOPMAX3				-34
#define TCOPMAX4				0

#define FRMAX					5555
#define FSMAX					999

#define EXPECT(r, s, tco)		(r * m->Rad1h / 1000 + s * (100 - m->SunD1) / 10) * (tco * (m->TTT - 25) + 10000) / 10000

#define SUM_EXP(m)				((m)->exp1 + (m)->exp2 + (m)->exp3 + (m)->exp4)
#define SUM_MPPT(m)				((m)->mppt1 + (m)->mppt2 + (m)->mppt3 + (m)->mppt4)

#define NOISE					10

#define HISTORY_SIZE			(24 * 7)

// all raw values from kml file
static mosmix_csv_t mosmix_csv[256];

// 24h slots over one week and access pointers
// today and tommorow contains only forecast data
// history contains all data (forecast, mppt, factors and errors)
static mosmix_t today[24], tomorrow[24], history[HISTORY_SIZE];
#define TODAY(h)				(&today[h])
#define TOMORROW(h)				(&tomorrow[h])
#define HISTORY(d, h)			(&history[24 * d + h])

// rad1/sund1 coefficients per MPPT and hour and access pointer
static factor_t factors[24];
#define FACTORS(h)				(&factors[h])

// fake dummy average akkus over 24/7
static int fake_loads[24] = { 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173 };

static void sum(mosmix_t *to, mosmix_t *from) {
	int *t = (int*) to;
	int *f = (int*) from;
	for (int i = 0; i < MOSMIX_SIZE; i++) {
		*t = *t + *f;
		t++;
		f++;
	}
}
static void parse(char **strings, size_t size) {
	int idx = atoi(strings[0]);
	mosmix_csv_t *m = &mosmix_csv[idx];

	m->idx = idx;
	m->ts = atoi(strings[1]);
	m->TTT = atof(strings[2]) - 273.15; // convert to °C
	m->Rad1h = atoi(strings[3]);
	m->SunD1 = atoi(strings[4]) * 100 / 3600; // convert to 0..100 percent
	m->RSunD = atoi(strings[5]);
}

static int expect(mosmix_t *m, int r, int s, int tco) {
	int x = EXPECT(r, s, tco);
	if (x < 0) {
		xdebug("MOSMIX expected < 0, recalculating with s = 0");
		x = EXPECT(r, 0, tco);
	}
	return x > NOISE ? x : 0;
}

// calculate expected pv as combination of raw mosmix values with mppt specific coefficients
static void expecteds(mosmix_t *m, factor_t *f) {
	m->exp1 = expect(m, f->r1, f->s1, TCOPMAX1);
	m->exp2 = expect(m, f->r2, f->s2, TCOPMAX2);
	m->exp3 = expect(m, f->r3, f->s3, TCOPMAX3);
	m->exp4 = expect(m, f->r4, f->s4, TCOPMAX4);
}

static void errors(mosmix_t *m) {
	// calculate errors as actual - expected
	m->diff1 = m->mppt1 - m->exp1;
	m->diff2 = m->mppt2 - m->exp2;
	m->diff3 = m->mppt3 - m->exp3;
	m->diff4 = m->mppt4 - m->exp4;

	// calculate errors as actual / expected
	m->err1 = m->exp1 && m->mppt1 ? m->mppt1 * 100 / m->exp1 : 100;
	m->err2 = m->exp2 && m->mppt2 ? m->mppt2 * 100 / m->exp2 : 100;
	m->err3 = m->exp3 && m->mppt3 ? m->mppt3 * 100 / m->exp3 : 100;
	m->err4 = m->exp4 && m->mppt4 ? m->mppt4 * 100 / m->exp4 : 100;
}

static void collect(struct tm *now, mosmix_t *mtomorrow, mosmix_t *mtoday, mosmix_t *msod, mosmix_t *meod) {
	ZEROP(mtomorrow);
	ZEROP(mtoday);
	ZEROP(msod);
	ZEROP(meod);

	for (int h = 0; h < 24; h++) {
		mosmix_t *m1 = TOMORROW(h);
		mosmix_t *m0 = TODAY(h);

		sum(mtomorrow, m1);
		sum(mtoday, m0);

		if (h < now->tm_hour + 1)
			// full elapsed hours into sod
			sum(msod, m0);
		else if (h > now->tm_hour + 1)
			// full remaining hours into eod
			sum(meod, m0);
		else {
			// current hour - split at current minute
			int xs1 = m0->exp1 * now->tm_min / 60, xe1 = m0->exp1 - xs1;
			int xs2 = m0->exp2 * now->tm_min / 60, xe2 = m0->exp2 - xs2;
			int xs3 = m0->exp3 * now->tm_min / 60, xe3 = m0->exp3 - xs3;
			int xs4 = m0->exp4 * now->tm_min / 60, xe4 = m0->exp4 - xs4;
			// elapsed minutes into sod
			msod->exp1 += xs1;
			msod->exp2 += xs2;
			msod->exp3 += xs3;
			msod->exp4 += xs4;
			// remaining minutes into eod
			meod->exp1 += xe1;
			meod->exp2 += xe2;
			meod->exp3 += xe3;
			meod->exp4 += xe4;
		}
	}
}

// recalc expected and errors
static void recalc_expected() {
	for (int h = 0; h < 24; h++) {
		factor_t *f = FACTORS(h);

		// today
		mosmix_t *m0 = TODAY(h);
		expecteds(m0, f);
		errors(m0);

		// tomorrow
		mosmix_t *m1 = TOMORROW(h);
		expecteds(m1, f);
		errors(m1);

		// history
		for (int d = 0; d < 7; d++) {
			mosmix_t *m = HISTORY(d, h);
			expecteds(m, f);
			errors(m);
		}
	}
}

// update today and tomorrow with actual data from mosmix kml download
static void update_today_tomorrow(struct tm *now) {
	struct tm tm;
	int day_today = now->tm_yday;
	int day_tomorrow = now->tm_yday < 365 ? now->tm_yday + 1 : 0;

	// loop over one week
	for (int i = 0; i < HISTORY_SIZE; i++) {
		mosmix_csv_t *mcsv = &mosmix_csv[i];

		// find slot to update
		localtime_r(&mcsv->ts, &tm);
		mosmix_t *m = 0;
		if (tm.tm_yday == day_today)
			m = TODAY(tm.tm_hour);
		if (tm.tm_yday == day_tomorrow)
			m = TOMORROW(tm.tm_hour);
		if (!m)
			continue; // not today or tomorrow

		// update
		m->Rad1h = mcsv->Rad1h;
		m->SunD1 = mcsv->SunD1;
		m->TTT = mcsv->TTT;
		if (m->Rad1h)
			xdebug("MOSMIX updated %02d.%02d. hour %02d Rad1h=%d SunD1=%d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, m->Rad1h, m->SunD1);
		else
			// SunD1 without Rad1h is not possible
			m->SunD1 = 0;
	}
}

static void* calculate_factors_slave(void *arg) {
	int *h = (int*) arg;

	xdebug("MOSMIX factors thread started hour=%d", *h);

	// before going into loops: check if we have Rad1h for this hour
	int rad1h = 0;
	for (int d = 0; d < 7; d++)
		rad1h += HISTORY(d, *h)->Rad1h;
	if (!rad1h) {
		*h = -1;
		return (void*) 0;
	}

	factor_t *f = FACTORS(*h);
	f->e1 = f->e2 = f->e3 = f->e4 = INT16_MAX;

	for (int r = 0; r <= FRMAX; r++) {
		for (int s = -FSMAX; s <= FSMAX; s++) {

			// sum up errors over one week
			int e1 = 0, e2 = 0, e3 = 0, e4 = 0;
			for (int d = 0; d < 7; d++) {
				mosmix_t *m = HISTORY(d, *h);

				// calculate expected
				int exp1 = EXPECT(r, s, TCOPMAX1);
				int exp2 = EXPECT(r, s, TCOPMAX2);
				int exp3 = EXPECT(r, s, TCOPMAX3);
				int exp4 = EXPECT(r, s, TCOPMAX4);

//				if (r == 100 && s == 100)
//					xdebug("MOSMIX Rad1h=%d SunD1=%d TTT=%d r=s=100 exp1=%d exp2=%d exp3=%d exp4=%d", m->Rad1h, m->SunD1, m->TTT, exp1, exp2, exp3, exp4);

				// calculate absolute error
				if (exp1 < 0)
					e1 += INT16_MAX;
				else
					e1 += m->mppt1 > exp1 ? (m->mppt1 - exp1) : (exp1 - m->mppt1);

				if (exp2 < 0)
					e2 += INT16_MAX;
				else
					e2 += m->mppt2 > exp2 ? (m->mppt2 - exp2) : (exp2 - m->mppt2);

				if (exp3 < 0)
					e3 += INT16_MAX;
				else
					e3 += m->mppt3 > exp3 ? (m->mppt3 - exp3) : (exp3 - m->mppt3);

				if (exp4 < 0)
					e4 += INT16_MAX;
				else
					e4 += m->mppt4 > exp4 ? (m->mppt4 - exp4) : (exp4 - m->mppt4);
			}

			// take over coefficients from the smallest error
			if (e1 < f->e1) {
				f->r1 = r;
				f->s1 = s;
				f->e1 = e1;
			}
			if (e2 < f->e2) {
				f->r2 = r;
				f->s2 = s;
				f->e2 = e2;
			}
			if (e3 < f->e3) {
				f->r3 = r;
				f->s3 = s;
				f->e3 = e3;
			}
			if (e4 < f->e4) {
				f->r4 = r;
				f->s4 = s;
				f->e4 = e4;
			}
		}
	}

	// fix disconnected MPPT4 noise
	f->r4 = f->s4 = f->e4 = 0;

	// indicate finish
	*h = -1;
	return (void*) 0;
}

static void* calculate_factors_master(void *arg) {
	char line[LINEBUF], value[10];

	// 24 thread and control slots
	pthread_t threads[24];
	int control[24];

	// set working hour for slot
	for (int h = 0; h < 24; h++)
		control[h] = h;

	ZERO(factors);
	ZERO(threads);

	// how many processors can we use
	int nprocs = get_nprocs();

	while (1) {
		strcpy(line, "MOSMIX factors thread control ");
		int completed = 1;
		for (int h = 0; h < 24; h++) {
			if (control[h] == -1) {
				// finished - join thread and free processor
				if (threads[h])
					if (!pthread_join(threads[h], NULL)) {
						threads[h] = 0;
						nprocs++;
					}
			} else {
				// start thread if we have a free processor
				completed = 0;
				if (!threads[h] && nprocs > 0)
					if (!pthread_create(&threads[h], NULL, &calculate_factors_slave, &control[h]))
						nprocs--;
			}
			snprintf(value, 10, " %2d", control[h]);
			strcat(line, value);
		}
		xdebug(line);

		if (completed)
			break;

		sleep(1);
	}

#ifndef MOSMIX_MAIN
	store_blob(STATE SLASH MOSMIX_FACTORS, factors, sizeof(factors));
#endif
	mosmix_t mc;
	store_table_csv(factors, FACTOR_SIZE, 24, FACTOR_HEADER, RUN SLASH MOSMIX_FACTORS_CSV);
	dump_table(factors, FACTOR_SIZE, 24, 0, "MOSMIX factors", FACTOR_HEADER);
	cumulate(&mc, factors, FACTOR_SIZE, 24);
	dump_array(&mc, FACTOR_SIZE, "[++]", 0);
	return (void*) 0;
}

// brute force coefficients determination per MPPT and hour in separate thread
void mosmix_factors(int wait) {
	pthread_t master;

	if (pthread_create(&master, NULL, &calculate_factors_master, NULL))
		xerr("MOSMIX Error creating calculate_factors_master thread");

	if (!wait)
		return;

	if (pthread_join(master, NULL) != 0)
		xerr("MOSMIX Error joining calculate_factors_master thread");
}

void mosmix_mppt(struct tm *now, int mppt1, int mppt2, int mppt3, int mppt4) {
	mosmix_t *m = TODAY(now->tm_hour);

	// update actual pv
	m->mppt1 = mppt1 > NOISE ? mppt1 : 0;
	m->mppt2 = mppt2 > NOISE ? mppt2 : 0;
	m->mppt3 = mppt3 > NOISE ? mppt3 : 0;
	m->mppt4 = mppt4 > NOISE ? mppt4 : 0;

	// recalc errors
	errors(m);
	xdebug("MOSMIX forecast mppt Wh %5d %5d %5d %5d sum %d", m->mppt1, m->mppt2, m->mppt3, m->mppt4, m->mppt1 + m->mppt2 + m->mppt3 + m->mppt4);
	xdebug("MOSMIX forecast exp  Wh %5d %5d %5d %5d sum %d", m->exp1, m->exp2, m->exp3, m->exp4, m->exp1 + m->exp2 + m->exp3 + m->exp4);
	xdebug("MOSMIX forecast err  Wh %5d %5d %5d %5d sum %d", m->diff1, m->diff2, m->diff3, m->diff4, m->diff1 + m->diff2 + m->diff3 + m->diff4);
	xdebug("MOSMIX forecast err  %%  %5.2f %5.2f %5.2f %5.2f", FLOAT100(m->err1), FLOAT100(m->err2), FLOAT100(m->err3), FLOAT100(m->err4));

	// save to history
	mosmix_t *mh = HISTORY(now->tm_wday, now->tm_hour);
	memcpy(mh, m, sizeof(mosmix_t));
}

// collect total expected today, tomorrow and till end of day / start of day
void mosmix_collect(struct tm *now, int *itomorrow, int *itoday, int *isod, int *ieod) {
	mosmix_t mtoday, mtomorrow, msod, meod;
	collect(now, &mtomorrow, &mtoday, &msod, &meod);

	*itomorrow = SUM_EXP(&mtomorrow);
	*itoday = SUM_EXP(&mtoday);
	*isod = SUM_EXP(&msod);
	*ieod = SUM_EXP(&meod);
	xdebug("MOSMIX tomorrow=%d today=%d sod=%d eod=%d", *itomorrow, *itoday, *isod, *ieod);

	// validate
	if (*itoday != *isod + *ieod)
		xdebug("MOSMIX sod/eod calculation error %d != %d + %d", *itoday, *isod, *ieod);
}

void mosmix_scale(struct tm *now, int *succ1, int *succ2) {
	mosmix_t mtoday, mtomorrow, msod, meod;
	*succ1 = *succ2 = 0;

	// before scale
	collect(now, &mtomorrow, &mtoday, &msod, &meod);
	int exp1 = SUM_EXP(&mtoday);
	int sodx1 = SUM_EXP(&msod);
	int sodm1 = SUM_MPPT(&msod);
	*succ1 = sodx1 ? sodm1 * 1000 / sodx1 : 0;

	// nothing to scale
	if (TODAY(now->tm_hour)->Rad1h == 0)
		return;

	int ch = now->tm_hour + 1;
	mosmix_t m;
	ZERO(m);

	// sum up mppt and expected till now
	for (int h = 0; h < ch; h++)
		sum(&m, TODAY(h));

	// calculate diff and error
	errors(&m);

	// thresholds for scaling
	int s1 = (m.diff1 < -100 || m.diff1 > 100) && (m.err1 < 90 || m.err1 > 120);
	int s2 = (m.diff2 < -100 || m.diff2 > 100) && (m.err2 < 90 || m.err2 > 120);
	int s3 = (m.diff3 < -100 || m.diff3 > 100) && (m.err3 < 90 || m.err3 > 120);
	int s4 = (m.diff4 < -100 || m.diff4 > 100) && (m.err4 < 90 || m.err4 > 120);

	if (s1)
		xlog("MOSMIX scaling MPPT1 by %5.2f (%s%d)", FLOAT100(m.err1), m.diff1 > 0 ? "+" : "", m.diff1);
	if (s2)
		xlog("MOSMIX scaling MPPT2 by %5.2f (%s%d)", FLOAT100(m.err2), m.diff2 > 0 ? "+" : "", m.diff2);
	if (s3)
		xlog("MOSMIX scaling MPPT3 by %5.2f (%s%d)", FLOAT100(m.err3), m.diff3 > 0 ? "+" : "", m.diff3);
	if (s4)
		xlog("MOSMIX scaling MPPT4 by %5.2f (%s%d)", FLOAT100(m.err4), m.diff4 > 0 ? "+" : "", m.diff4);

	// nothing to scale
	if (!s1 && !s2 && !s3 && !s4)
		return;

	// scale all sod and eod - this corrects the success factor to ~100%
	for (int h = 0; h < 24; h++) {
		if (s1)
			TODAY(h)->exp1 = TODAY(h)->exp1 * m.err1 / 100;
		if (s2)
			TODAY(h)->exp2 = TODAY(h)->exp2 * m.err2 / 100;
		if (s3)
			TODAY(h)->exp3 = TODAY(h)->exp3 * m.err3 / 100;
		if (s4)
			TODAY(h)->exp4 = TODAY(h)->exp4 * m.err4 / 100;
	}

	// after scale
	collect(now, &mtomorrow, &mtoday, &msod, &meod);
	int exp2 = SUM_EXP(&mtoday);
	int sodx2 = SUM_EXP(&msod);
	int sodm2 = SUM_MPPT(&msod);
	*succ2 = sodx2 ? sodm2 * 1000 / sodx2 : 0;

	float fs1 = FLOAT10(*succ1), fs2 = FLOAT10(*succ2);
	xlog("MOSMIX scaling   before: total=%d mppt=%d exp=%d succ=%.1f%%   after: total=%d mppt=%d exp=%d succ=%.1f%%", exp1, sodm1, sodx1, fs1, exp2, sodm2, sodx2, fs2);
}

// night: collect akku power when pv is not enough
int mosmix_survive(struct tm *now, int aload[], int aakku[]) {
	char line[LINEBUF * 2], value[48];
	int ch = now->tm_hour < 23 ? now->tm_hour + 1 : 0, h = ch, night = 0, midnight = 0, hours = 0, needed = 0;

	strcpy(line, "MOSMIX survive h:l:a:x");
	while (1) {
		mosmix_t *m = midnight ? TOMORROW(h) : TODAY(h);

		// current hour -> partly, remaining hours -> full
		int l = h == ch ? aload[h] * (60 - now->tm_min) / 60 : aload[h];
		int a = h == ch ? aakku[h] * (60 - now->tm_min) / 60 : aakku[h];
		int x = h == ch ? SUM_EXP(m) * (60 - now->tm_min) / 60 : SUM_EXP(m);

		// night
		if (l > x) {
			snprintf(value, 48, " %d:%d:%d:%d", h, l, a, x);
			strcat(line, value);
			needed += l > a ? l : a; // use higher one - akku might be empty or still some pv available
			night = 1;
			hours++;
		}

		// reached end of night or high noon this/next day
		if ((night && x > l) || (night && h == 12) || (midnight && h == 12))
			break;

		// reached midnight
		if (++h == 24) {
			midnight = 1;
			h = 0;
		}
	}

	snprintf(value, 48, " --> %d hours = %d", hours, needed);
	strcat(line, value);
	xlog(line);
	return needed;
}

// day: collect heating power where we can use pv for
int mosmix_heating(struct tm *now, int power) {
	char line[LINEBUF], value[48];
	int ch = now->tm_hour < 23 ? now->tm_hour + 1 : 0, hours = 0, needed = 0;

	strcpy(line, "MOSMIX heating h:x:p");
	for (int h = ch; h < 24; h++) {
		mosmix_t *m = TODAY(h);
		// current hour -> partly, remaining hours -> full
		int p = h == ch ? power * (60 - now->tm_min) / 60 : power;
		int x = h == ch ? SUM_EXP(m) * (60 - now->tm_min) / 60 : SUM_EXP(m);
		if (x > p) {
			snprintf(value, 48, " %d:%d:%d", h, x, p);
			strcat(line, value);
			needed += p;
			hours++;
		}
	}

	snprintf(value, 48, " --> %d hours = %d", hours, needed);
	strcat(line, value);
	xdebug(line);
	return needed;
}

// sum up 24 mosmix slots for one day (with offset)
void mosmix_24h(int day, mosmix_csv_t *sum) {
	LOCALTIME

	// calculate today 0:00:00 as start and +24h as end time frame
	now->tm_hour = now->tm_min = now->tm_sec = 0;
	time_t ts_from = mktime(now) + 60 * 60 * 24 * day;
	time_t ts_to = ts_from + 60 * 60 * 24; // + 1 day

	ZEROP(sum);
	for (int i = 0; i < ARRAY_SIZE(mosmix_csv); i++) {
		mosmix_csv_t *m = &mosmix_csv[i];
		if (ts_from < m->ts && m->ts <= ts_to) {
			sum->Rad1h += m->Rad1h;
			sum->SunD1 += m->SunD1;
			if (m->RSunD)
				sum->RSunD = m->RSunD;	// last 24 hours calculated at 0 and 6
		}
	}
}

void mosmix_dump_today(struct tm *now) {
	mosmix_t m;
	cumulate(&m, today, MOSMIX_SIZE, 24);
	dump_table(today, MOSMIX_SIZE, 24, now->tm_hour, "MOSMIX today", MOSMIX_HEADER);
	dump_array(&m, MOSMIX_SIZE, "[++]", 0);
}

void mosmix_dump_tomorrow(struct tm *now) {
	mosmix_t m;
	cumulate(&m, tomorrow, MOSMIX_SIZE, 24);
	dump_table(tomorrow, MOSMIX_SIZE, 24, now->tm_hour, "MOSMIX tomorrow", MOSMIX_HEADER);
	dump_array(&m, MOSMIX_SIZE, "[++]", 0);
}

void mosmix_dump_history(struct tm *now) {
	dump_table(history, MOSMIX_SIZE, HISTORY_SIZE, now->tm_wday * 24 + now->tm_hour, "MOSMIX history full", MOSMIX_HEADER);
}

void mosmix_dump_history_hours(int h) {
	xlog("MOSMIX history hours\n    "MOSMIX_HEADER);
	char idx[6];
	snprintf(idx, 6, "[%02d]", h);
	for (int d = 0; d < 7; d++)
		dump_array(HISTORY(d, h), MOSMIX_SIZE, idx, 0);
}

void mosmix_load_state(struct tm *now) {
	load_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));

	// initially fill today and tomorrow from history
	ZERO(today);
	ZERO(tomorrow);
	int day_today = now->tm_wday;
	int day_tomorrow = day_today < 6 ? day_today + 1 : 0;
	for (int h = 0; h < 24; h++) {
		memcpy(TODAY(h), HISTORY(day_today, h), sizeof(mosmix_t));
		memcpy(TOMORROW(h), HISTORY(day_tomorrow, h), sizeof(mosmix_t));
	}

	// load or calculate factors
	if (access(STATE SLASH MOSMIX_FACTORS, F_OK))
		mosmix_factors(0);
	else
		load_blob(STATE SLASH MOSMIX_FACTORS, factors, sizeof(factors));
}

void mosmix_store_state() {
	store_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));
	store_blob(STATE SLASH MOSMIX_FACTORS, factors, sizeof(factors));
}
void mosmix_store_csv() {
	store_table_csv(factors, FACTOR_SIZE, 24, FACTOR_HEADER, RUN SLASH MOSMIX_FACTORS_CSV);
	store_table_csv(history, MOSMIX_SIZE, HISTORY_SIZE, MOSMIX_HEADER, RUN SLASH MOSMIX_HISTORY_CSV);
	store_table_csv(today, MOSMIX_SIZE, 24, MOSMIX_HEADER, RUN SLASH MOSMIX_TODAY_CSV);
	store_table_csv(tomorrow, MOSMIX_SIZE, 24, MOSMIX_HEADER, RUN SLASH MOSMIX_TOMORROW_CSV);
}

int mosmix_load(struct tm *now, const char *filename, int clear) {
	char *strings[MOSMIX_COLUMNS];
	char buf[LINEBUF];

	ZERO(mosmix_csv);

	FILE *fp = fopen(filename, "rt");
	if (fp == NULL)
		return xerr("MOSMIX Cannot open file %s for reading", filename);

	// header
	if (fgets(buf, LINEBUF, fp) == NULL)
		return xerr("MOSMIX no data available");

	int lines = 0;
	while (fgets(buf, LINEBUF, fp) != NULL) {
		int tokens = 0;
		char *p = strtok(buf, ",");
		while (p != NULL) {
			strings[tokens] = p;
			p = strtok(NULL, ",");
			tokens++;
		}
		parse(strings, ARRAY_SIZE(strings));
		lines++;
	}

	fclose(fp);
	xlog("MOSMIX loaded %s containing %d lines", filename, lines);

	if (clear) {
		ZERO(today);
		ZERO(tomorrow);
	}
	update_today_tomorrow(now);
	recalc_expected();
	return 0;
}

static int test() {
	LOCALTIME

	int x = 3333;
	int f = 222;
	printf("io  %d\n", x * f / 100);
	printf("io  %d\n", (x * f) / 100);
	printf("nio %d\n", x * (f / 100));

	// load state and update forecasts
	mosmix_load_state(now);
	mosmix_load(now, WORK SLASH MARIENBERG, 1);

	// calculate total daily values
	mosmix_csv_t m0, m1, m2;
	mosmix_24h(0, &m0);
	mosmix_24h(1, &m1);
	mosmix_24h(2, &m2);
	xlog("MOSMIX Rad1h/SunD1/RSunD today %d/%d/%d tomorrow %d/%d/%d tomorrow+1 %d/%d/%d", m0.Rad1h, m0.SunD1, m0.RSunD, m1.Rad1h, m1.SunD1, m1.RSunD, m2.Rad1h, m2.SunD1, m2.RSunD);

	int itoday, itomorrow, sod, eod, succ1, succ2;

	// calculate expected today and tomorrow
	xlog("MOSMIX *** now (%02d) ***", now->tm_hour);
	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod);
	mosmix_dump_today(now);
	mosmix_dump_tomorrow(now);

	xlog("MOSMIX *** updated now (%02d) ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_scale(now, &succ1, &succ2);
	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod);

	now->tm_hour = 9;
	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_scale(now, &succ1, &succ2);
	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod);

	now->tm_hour = 12;
	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_scale(now, &succ1, &succ2);
	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod);

	now->tm_hour = 15;
	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_scale(now, &succ1, &succ2);
	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod);

	mosmix_dump_history(now);
	mosmix_dump_history_hours(9);
	mosmix_dump_history_hours(12);
	mosmix_dump_history_hours(15);

	now->tm_hour = 16;
	mosmix_dump_today(now);
	mosmix_dump_tomorrow(now);
	mosmix_survive(now, fake_loads, fake_loads);
	mosmix_heating(now, 1500);
	return 0;
}

static int recalc() {
	LOCALTIME

	mosmix_load_state(now);
	mosmix_load(now, WORK SLASH MARIENBERG, 0);
	mosmix_factors(1);
	recalc_expected();
	mosmix_dump_history_hours(12);
	mosmix_store_csv();
	return 0;
}

static int migrate() {
	ZERO(history);

	mosmix_old_t old[HISTORY_SIZE];
	load_blob(STATE SLASH MOSMIX_HISTORY, old, sizeof(old));

	for (int i = 0; i < HISTORY_SIZE; i++) {
		mosmix_old_t *o = &old[i];
		mosmix_t *n = &history[i];
		n->Rad1h = o->Rad1h;
		n->SunD1 = o->SunD1;
		n->TTT = o->TTT;
		n->mppt1 = o->mppt1;
		n->mppt2 = o->mppt2;
		n->mppt3 = o->mppt3;
		n->mppt4 = o->mppt4;
	}

	// test and verify
	store_blob(TMP SLASH MOSMIX_HISTORY, history, sizeof(history));
	// live
	// store_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));
	return 0;
}

static int fix() {
	load_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));

//	mosmix_t *m;
//	m = &history[152];
//	m->mppt3 = m->diff3 = 0;
//	m->err3 = 100;
//	m = &history[139];
//	m->mppt1 = m->mppt2 = m->mppt3 = m->mppt4 = 0;
//	m->diff1 = m->diff2 = m->diff3 = m->diff4 = 0;
//	m->err1 = m->err2 = m->err3 = m->err4 = 100;
//	m = &history[140];
//	m->mppt1 = m->mppt2 = m->mppt3 = m->mppt4 = 0;
//	m->diff1 = m->diff2 = m->diff3 = m->diff4 = 0;
//	m->err1 = m->err2 = m->err3 = m->err4 = 100;

	store_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));
	return 0;
}

static void wget(struct tm *now, const char *id) {
	char ftstamp[32], fname[64], fforecasts[64], ftimestamps[64], furl[256], cmd[288];
	chdir(TMP);

	snprintf(ftstamp, 32, "%4d%02d%02d%02d", now->tm_year + 1900, now->tm_mon + 1, now->tm_mday, 3);
	printf("File timestamp %s\n", ftstamp);
	snprintf(fname, 64, "MOSMIX_L_%s_%s.kmz", ftstamp, id);
	printf("File name %s\n", fname);
	snprintf(furl, 256, "http://opendata.dwd.de/weather/local_forecasts/mos/MOSMIX_L/single_stations/%s/kml/%s", id, fname);
	printf("File path %s\n", furl);

	snprintf(cmd, 288, "wget -q %s", furl);
	system(cmd);
	snprintf(cmd, 288, "unzip -q -o %s", fname);
	system(cmd);

	snprintf(ftimestamps, 64, "mosmix-timestamps-%s.json", id);
	snprintf(cmd, 288, "/usr/local/bin/mosmix.py --in-file %s --out-file %s timestamps", fname, ftimestamps);
	system(cmd);

	snprintf(fforecasts, 64, "mosmix-forecasts-%s.json", id);
	snprintf(cmd, 288, "/usr/local/bin/mosmix.py --in-file %s --out-file %s forecasts", fname, fforecasts);
	system(cmd);

	snprintf(cmd, 288, "/usr/local/bin/mosmix-json2csv.sh %s TTT Rad1h SunD1 RSunD", id);
	system(cmd);
}

static int diffs(struct tm *now) {
	// take over mppt's from history into today
	for (int h = 0; h < 24; h++) {
		mosmix_t *mh = HISTORY(now->tm_wday, h);
		mosmix_t *mt = TODAY(h);
		mt->mppt1 = mh->mppt1;
		mt->mppt2 = mh->mppt2;
		mt->mppt3 = mh->mppt3;
		mt->mppt4 = mh->mppt4;
	}

	int diff_sum = 0;
	for (int h = 0; h < 24; h++) {
		mosmix_t *m = TODAY(h);
		factor_t *f = FACTORS(h);
		expecteds(m, f);
		int mppt = SUM_MPPT(m);
		int expt = SUM_EXP(m);
		int diff = mppt - expt;
		diff_sum += abs(diff);
		if (diff)
			printf("hour %02d mppt %4d expt %4d   --> err %4d\n", h, mppt, expt, diff);
	}
	return diff_sum;
}

static int compare() {
	load_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));
	load_blob(STATE SLASH MOSMIX_FACTORS, factors, sizeof(factors));

	// yesterday
	time_t ts_yday = time(NULL);
	ts_yday -= 60 * 60 * 24;
	struct tm tm_yday, *yday = &tm_yday;
	localtime_r(&ts_yday, &tm_yday);

	wget(yday, "10579");
	mosmix_load(yday, TMP SLASH MARIENBERG, 1);
	int dm = diffs(yday);

	wget(yday, "10577");
	mosmix_load(yday, TMP SLASH CHEMNITZ, 1);
	int dc = diffs(yday);

	wget(yday, "N4464");
	mosmix_load(yday, TMP SLASH BRAUNSDORF, 1);
	int db = diffs(yday);

	printf("%4d-%02d-%02d Marienberg=%d Chemnitz=%d Braunsdorf=%d\n", yday->tm_year + 1900, yday->tm_mon + 1, yday->tm_mday, dm, dc, db);
	return 0;
}

int mosmix_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	// no arguments - test
	if (argc == 1)
		return test();

	int c;
	while ((c = getopt(argc, argv, "cfmrt")) != -1) {
		// printf("getopt %c\n", c);
		switch (c) {
		case 'c':
			return compare();
		case 'f':
			return fix();
		case 'm':
			return migrate();
		case 'r':
			return recalc();
		case 't':
			return test();
		default:
			xlog("unknown getopt %c", c);
		}
	}

	return 0;
}

#ifdef MOSMIX_MAIN
int main(int argc, char **argv) {
	return mosmix_main(argc, argv);
}
#endif
