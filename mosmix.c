#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

// gcc -Wall -DMOSMIX_MAIN -I ./include/ -o mosmix mosmix.c utils.c

#define SUM_EXP					(m->exp1 + m->exp2 + m->exp3 + m->exp4)
#define SUM_MPPT				(m->mppt1 + m->mppt2 + m->mppt3 + m->mppt4)

#define SUND1_MINIMUM			300

// all values
static mosmix_csv_t mosmix_csv[256];

// 24h slots over one week and access pointers
// today and tommorow contains only forecast data (base+expected)
// history contains all data (forecast, mppt, factors and errors)
static mosmix_t today[24], tomorrow[24], history[24 * 7];
#define TODAY(h)				(&today[h])
#define TOMORROW(h)				(&tomorrow[h])
#define HISTORY(d, h)			(&history[24 * d + h])

// string specific base factors
static int rad1h_1, rad1h_2, rad1h_3, rad1h_4;
static int sund1_1, sund1_2, sund1_3, sund1_4;

static void scale1(struct tm *now, mosmix_t *m) {
	float f = FLOAT100(m->err1 / 2);
	xlog("MOSMIX scaling today's remaining expected MPPT1 values by %5.2f", f);
	for (int h = now->tm_hour + 1; h < 24; h++)
		TODAY(h)->exp1 *= f;
}

static void scale2(struct tm *now, mosmix_t *m) {
	float f = FLOAT100(m->err2 / 2);
	xlog("MOSMIX scaling today's remaining expected MPPT2 values by %5.2f", f);
	for (int h = now->tm_hour + 1; h < 24; h++)
		TODAY(h)->exp2 *= f;
}

static void scale3(struct tm *now, mosmix_t *m) {
	float f = FLOAT100(m->err3 / 2);
	xlog("MOSMIX scaling today's remaining expected MPPT3 values by %5.2f", f);
	for (int h = now->tm_hour + 1; h < 24; h++)
		TODAY(h)->exp3 *= f;
}

static void scale4(struct tm *now, mosmix_t *m) {
	float f = FLOAT100(m->err4 / 2);
	xlog("MOSMIX scaling today's remaining expected MPPT4 values by %5.2f", f);
	for (int h = now->tm_hour + 1; h < 24; h++)
		TODAY(h)->exp4 *= f;
}

static void parse(char **strings, size_t size) {
	int idx = atoi(strings[0]);
	mosmix_csv_t *m = &mosmix_csv[idx];

	m->idx = idx;
	m->ts = atoi(strings[1]);
	m->TTT = atof(strings[2]) - 273.15;
	m->Rad1h = atoi(strings[3]);
	m->SunD1 = atoi(strings[4]);
	m->RSunD = atoi(strings[5]);
}

// calculate base value as combination of raw mosmix values with string specific factors
static void base(mosmix_t *m) {
	m->base1 = m->Rad1h * (float) rad1h_1 / 100 + m->SunD1 * (float) sund1_1 / 100;
	m->base2 = m->Rad1h * (float) rad1h_2 / 100 + m->SunD1 * (float) sund1_2 / 100;
	m->base3 = m->Rad1h * (float) rad1h_3 / 100 + m->SunD1 * (float) sund1_3 / 100;
	m->base4 = m->Rad1h * (float) rad1h_4 / 100 + m->SunD1 * (float) sund1_4 / 100;
}

// calculate expected value derived from base with factors for each hour
static void expected(mosmix_t *m, mosmix_t *fcts) {
	m->exp1 = m->base1 * (FLOAT100(fcts->fac1));
	m->exp2 = m->base2 * (FLOAT100(fcts->fac2));
	m->exp3 = m->base3 * (FLOAT100(fcts->fac3));
	m->exp4 = m->base4 * (FLOAT100(fcts->fac4));
}

static void sum(mosmix_t *to, mosmix_t *from) {
	int *t = (int*) to;
	int *f = (int*) from;
	for (int i = 0; i < MOSMIX_SIZE; i++) {
		*t = *t + *f;
		t++;
		f++;
	}
}
// calculate average factors per hour
static void average(mosmix_t *avg, int h) {
	mosmix_t counts;
	ZERO(counts);
	for (int d = 0; d < 7; d++) {
		mosmix_t *m = HISTORY(d, h);
		sum(avg, m);
		if (m->fac1)
			counts.fac1++;
		if (m->fac2)
			counts.fac2++;
		if (m->fac3)
			counts.fac3++;
		if (m->fac4)
			counts.fac4++;
	}
	avg->fac1 = counts.fac1 ? avg->fac1 / counts.fac1 : 100;
	avg->fac2 = counts.fac2 ? avg->fac2 / counts.fac2 : 100;
	avg->fac3 = counts.fac3 ? avg->fac3 / counts.fac3 : 100;
	avg->fac4 = counts.fac4 ? avg->fac4 / counts.fac4 : 100;
	if (avg->fac1)
		xdebug("MOSMIX hour %02d average factors %5.2f %5.2f %5.2f %5.2f", h, FLOAT100(avg->fac1), FLOAT100(avg->fac2), FLOAT100(avg->fac3), FLOAT100(avg->fac4));
}

static void base_factors(int h) {
	// check if we have at least one day with sunshine / (nearly) no sunshine
	int rad1h_ok = 0, sund1_ok = 0;
	for (int d = 0; d < 7; d++) {
		mosmix_t *m = HISTORY(d, h);
		if (m->SunD1 < SUND1_MINIMUM)
			rad1h_ok = 1;
		if (m->SunD1 > SUND1_MINIMUM)
			sund1_ok = 1;
	}

	if (!rad1h_ok) {
		// initial values determined in January 2025 from MPPT1
		xdebug("MOSMIX no day found with SunD1 < %d at hour %d, using initial values", SUND1_MINIMUM, h);
		rad1h_1 = rad1h_2 = rad1h_3 = rad1h_4 = 55;
		sund1_1 = sund1_2 = sund1_3 = sund1_4 = 122;
		return;
	}

	// find days with (nearly) no sunshine and calculate Rad1h factors
	rad1h_1 = rad1h_2 = rad1h_3 = rad1h_4 = 0;
	int rad1h_count = 0;
	for (int d = 0; d < 7; d++) {
		mosmix_t *m = HISTORY(d, h);
		if (m->SunD1 < SUND1_MINIMUM) {
			rad1h_count++;
			rad1h_1 += m->Rad1h && m->mppt1 ? m->mppt1 * 100 / m->Rad1h : 100;
			rad1h_2 += m->Rad1h && m->mppt2 ? m->mppt2 * 100 / m->Rad1h : 100;
			rad1h_3 += m->Rad1h && m->mppt3 ? m->mppt3 * 100 / m->Rad1h : 100;
			rad1h_4 += m->Rad1h && m->mppt4 ? m->mppt4 * 100 / m->Rad1h : 100;
		}
	}

	rad1h_1 /= rad1h_count;
	rad1h_2 /= rad1h_count;
	rad1h_3 /= rad1h_count;
	rad1h_4 /= rad1h_count;
	xdebug("MOSMIX hour %d Rad1h base factors %3d %3d %3d %3d count %d", h, rad1h_1, rad1h_2, rad1h_3, rad1h_4, rad1h_count);

	if (!sund1_ok) {
		// initial values determined in January 2025 from MPPT1
		sund1_1 = sund1_2 = sund1_3 = sund1_4 = 122;
		xdebug("MOSMIX no day found with SunD1 > %d at hour %d, using initial values", SUND1_MINIMUM, h);
		return;
	}

	// now find days with sunshine and calculate SunD1 factors
	int sund1_count = 0;
	sund1_1 = sund1_2 = sund1_3 = sund1_4 = 0;
	for (int d = 0; d < 7; d++) {
		mosmix_t *m = HISTORY(d, h);
		if (m->SunD1 > SUND1_MINIMUM) {
			sund1_count++;
			sund1_1 += m->SunD1 && m->mppt1 ? (m->mppt1 * 100 - m->Rad1h * rad1h_1) / m->SunD1 : 100;
			sund1_2 += m->SunD1 && m->mppt2 ? (m->mppt2 * 100 - m->Rad1h * rad1h_2) / m->SunD1 : 100;
			sund1_3 += m->SunD1 && m->mppt3 ? (m->mppt3 * 100 - m->Rad1h * rad1h_3) / m->SunD1 : 100;
			sund1_4 += m->SunD1 && m->mppt4 ? (m->mppt4 * 100 - m->Rad1h * rad1h_4) / m->SunD1 : 100;
		}
	}

	sund1_1 /= sund1_count;
	sund1_2 /= sund1_count;
	sund1_3 /= sund1_count;
	sund1_4 /= sund1_count;
	xdebug("MOSMIX hour %d SunD1 base factors %3d %3d %3d %3d count %d", h, sund1_1, sund1_2, sund1_3, sund1_4, sund1_count);
}

static void update_today_tomorrow() {
	// get todays and tomorrows year day
	struct tm tm;
	time_t t = time(NULL);
	localtime_r(&t, &tm);
	int day_today = tm.tm_yday;
	int day_tomorrow = tm.tm_yday != 365 ? tm.tm_yday + 1 : 0;

	// recalc base factors
	base_factors(11);
	base_factors(13);
	base_factors(12); // these are used
	xdebug("MOSMIX using base factors Rad1H/SunD1 %d/%d %d/%d %d/%d %d/%d", rad1h_1, sund1_1, rad1h_2, sund1_2, rad1h_3, sund1_3, rad1h_4, sund1_4);

	// loop over one week
	for (int i = 0; i < 24 * 7; i++) {
		mosmix_csv_t *mcsv = &mosmix_csv[i];

		// find mosmix slot to update
		t = mcsv->ts - 1; // fix hour
		localtime_r(&t, &tm);
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
		if (m->Rad1h)
			xdebug("MOSMIX updated %02d.%02d. hour %02d Rad1h=%d SunD1=%d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, m->Rad1h, m->SunD1);

		// calculate base
		base(m);
	}

	// calculate each mppt's forecast today and tomorrow
	mosmix_t avg;
	for (int h = 0; h < 24; h++) {
		ZERO(avg);
		average(&avg, h);
		mosmix_t *m0 = TODAY(h);
		mosmix_t *m1 = TOMORROW(h);
		expected(m0, &avg);
		expected(m1, &avg);
	}
}

static void calc(const char *id, int hour, int base, int exp, int mppt, int *err, int *fac) {
	// error actual vs. expected
	float e = exp ? (float) mppt / (float) exp : 1.0;
	*err = e * 100; // store as x100 scaled
	// factor actual vs. base
	float f = base ? (float) mppt / (float) base : 0.0;
	*fac = f * 100; // store as x100 scaled
	// xdebug("MOSMIX %s hour=%02d actual=%4d expected=%4d error=%5.2f factor=%5.2f", id, hour, mppt, exp, e, f);
}

void mosmix_mppt(struct tm *now, int mppt1, int mppt2, int mppt3, int mppt4) {
	mosmix_t *m = TODAY(now->tm_hour);

	// update actual pv
	m->mppt1 = mppt1;
	m->mppt2 = mppt2;
	m->mppt3 = mppt3;
	m->mppt4 = mppt4;

	// recalculate
	calc("MPPT1", now->tm_hour, m->base1, m->exp1, m->mppt1, &m->err1, &m->fac1);
	calc("MPPT2", now->tm_hour, m->base2, m->exp2, m->mppt2, &m->err2, &m->fac2);
	calc("MPPT3", now->tm_hour, m->base3, m->exp3, m->mppt3, &m->err3, &m->fac3);
	calc("MPPT4", now->tm_hour, m->base4, m->exp4, m->mppt4, &m->err4, &m->fac4);

	// validate today's forecast - if error is greater than 20% scale all remaining values
	if (m->err1 < 80 || m->err1 > 120)
		scale1(now, m);
	if (m->err2 < 80 || m->err2 > 120)
		scale2(now, m);
	if (m->err3 < 80 || m->err3 > 120)
		scale3(now, m);
	if (m->err4 < 80 || m->err4 > 120)
		scale4(now, m);

	// save to history
	mosmix_t *mh = HISTORY(now->tm_wday, now->tm_hour);
	memcpy(mh, m, sizeof(mosmix_t));
}

// collect total expected today, tomorrow and till end of day / start of day
void mosmix_collect(struct tm *now, int *itoday, int *itomorrow, int *sod, int *eod) {
	mosmix_t sum_today, sum_tomorrow, msod, meod;
	ZERO(sum_today);
	ZERO(sum_tomorrow);
	ZERO(msod);
	ZERO(meod);

	for (int h = 0; h < 24; h++) {
		mosmix_t *m0 = TODAY(h);
		mosmix_t *m1 = TOMORROW(h);
		sum(&sum_today, m0);
		sum(&sum_tomorrow, m1);
		if (h <= now->tm_hour)
			sum(&msod, m0);
		else
			sum(&meod, m0);
	}

	*itoday = sum_today.exp1 + sum_today.exp2 + sum_today.exp3 + sum_today.exp4;
	*itomorrow = sum_tomorrow.exp1 + sum_tomorrow.exp2 + sum_tomorrow.exp3 + sum_tomorrow.exp4;
	*sod = msod.exp1 + msod.exp2 + msod.exp3 + msod.exp4;
	*eod = meod.exp1 + meod.exp2 + meod.exp3 + meod.exp4;
	xlog("MOSMIX today=%d tomorrow=%d sod=%d eod=%d", *itoday, *itomorrow, *sod, *eod);

	// validate
	if (*itoday != *sod + *eod)
		xdebug("MOSMIX sod/eod calculation error %d != %d + %d", *itoday, *sod, *eod);
}

// calculate hours to survive next night
void mosmix_survive(struct tm *now, int min, int *hours, int *from, int *to) {
	int h, from_expected = 0, to_expected = 0;

	// find today sundown or now starting backwards from 0:00
	for (h = 23; h >= 12 && h > now->tm_hour; h--) {
		mosmix_t *m = TODAY(h);
		from_expected = SUM_EXP;
		if (from_expected > min)
			break;
	}
	*from = h != 12 ? h + 1 : h; // assume akku is not yet needed in that hour

	// find tomorrow sunrise
	for (h = 0; h < 12; h++) {
		mosmix_t *m = TOMORROW(h);
		to_expected = SUM_EXP;
		if (to_expected > min)
			break;
	}
	*to = h;

	*hours = 24 - *from + *to;
	xlog("MOSMIX survive hours=%d min=%d from=%d/%d to=%d/%d", *hours, min, *from, from_expected, *to, to_expected);
}

// calculate hours where we have enough power for heating
void mosmix_heating(struct tm *now, int min, int *hours, int *from, int *to) {
	int h, from_expected = 0, to_expected = 0;

	// find first hour with enough expected
	for (h = now->tm_hour; h < 24; h++) {
		mosmix_t *m = TODAY(h);
		from_expected = SUM_EXP;
		if (from_expected > min)
			break;
	}
	*from = h;

	// find last hour with enough expected
	for (h = 23; h >= now->tm_hour; h--) {
		mosmix_t *m = TODAY(h);
		to_expected = SUM_EXP;
		if (to_expected > min)
			break;
	}
	*to = h;

	// validate
	if (*from < *to)
		*hours = *to - *from;
	else
		*hours = *to = *from = to_expected = from_expected = 0;

	xlog("MOSMIX heating hours=%d min=%d from=%d/%d to=%d/%d", *hours, min, *from, from_expected, *to, to_expected);
}

// sum up 24 mosmix slots for one day (with offset)
void mosmix_24h(int day, mosmix_csv_t *sum) {
	struct tm tm;

	// calculate today 0:00:00 as start and +24h as end time frame
	time_t now_ts = time(NULL);
	localtime_r(&now_ts, &tm);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	time_t ts_from = mktime(&tm) + 60 * 60 * 24 * day;
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
	cumulate((int*) &m, (int*) today, MOSMIX_SIZE, 24);
	dump_table((int*) today, MOSMIX_SIZE, 24, now->tm_hour, "MOSMIX today", MOSMIX_HEADER);
	dump_struct((int*) &m, MOSMIX_SIZE, "[++]", 0);
}

void mosmix_dump_tomorrow(struct tm *now) {
	mosmix_t m;
	cumulate((int*) &m, (int*) tomorrow, MOSMIX_SIZE, 24);
	dump_table((int*) tomorrow, MOSMIX_SIZE, 24, now->tm_hour, "MOSMIX tomorrow", MOSMIX_HEADER);
	dump_struct((int*) &m, MOSMIX_SIZE, "[++]", 0);
}

void mosmix_dump_history_today(struct tm *now) {
	dump_table((int*) HISTORY(now->tm_wday, 0), MOSMIX_SIZE, 24, now->tm_hour, "MOSMIX history today", MOSMIX_HEADER);
}

void mosmix_dump_history_full(struct tm *now) {
	dump_table((int*) history, MOSMIX_SIZE, 24 * 7, now->tm_wday * 24 + now->tm_hour, "MOSMIX history full", MOSMIX_HEADER);
}

void mosmix_dump_history_noon() {
	xlog("MOSMIX history noon\n    "MOSMIX_HEADER);
	for (int i = 0; i < 7; i++)
		dump_struct((int*) HISTORY(i, 12), MOSMIX_SIZE, "[12]", 0);
}

void mosmix_clear_today_tomorrow() {
	ZERO(today);
	ZERO(tomorrow);
}

void mosmix_load_state() {
	ZERO(history);
	load_blob(MOSMIX_HISTORY, history, sizeof(history));
}

void mosmix_store_state() {
	store_blob(MOSMIX_HISTORY, history, sizeof(history));
}
void mosmix_store_csv() {
	store_csv((int*) history, MOSMIX_SIZE, 24 * 7, MOSMIX_HEADER, MOSMIX_HISTORY_CSV);
	store_csv((int*) today, MOSMIX_SIZE, 24, MOSMIX_HEADER, MOSMIX_TODAY_CSV);
	store_csv((int*) tomorrow, MOSMIX_SIZE, 24, MOSMIX_HEADER, MOSMIX_TOMORROW_CSV);
}

int mosmix_load(const char *filename) {
	char *strings[MOSMIX_COLUMNS];
	char buf[LINEBUF];

	ZERO(mosmix_csv);

	FILE *fp = fopen(filename, "r");
	if (fp == NULL)
		return xerr("MOSMIX Cannot open file %s for reading", filename);

	// header
	if (fgets(buf, LINEBUF, fp) == NULL)
		return xerr("MOSMIX no data available");

	int lines = 0;
	while (fgets(buf, LINEBUF, fp) != NULL) {
		int i = 0;
		char *p = strtok(buf, ",");
		while (p != NULL && i < MOSMIX_COLUMNS) {
			p[strcspn(p, "\n")] = 0; // remove any NEWLINE
			strings[i++] = p;
			p = strtok(NULL, ",");
		}
		parse(strings, ARRAY_SIZE(strings));
		lines++;
	}

	fclose(fp);
	xlog("MOSMIX loaded %s containing %d lines", filename, lines);

	update_today_tomorrow();
	return 0;
}

static void recalc() {
	ZERO(history);
	load_blob(MOSMIX_HISTORY, history, sizeof(history));

	// recalc base factors
	base_factors(11);
	base_factors(13);
	base_factors(12); // these are used

	// recalc base and factors
	for (int i = 0; i < 24 * 7; i++) {
		mosmix_t *m = &history[i];
		base(m);
		calc("MPPT1", i, m->base1, m->exp1, m->mppt1, &m->err1, &m->fac1);
		calc("MPPT2", i, m->base2, m->exp2, m->mppt2, &m->err2, &m->fac2);
		calc("MPPT3", i, m->base3, m->exp3, m->mppt3, &m->err3, &m->fac3);
		calc("MPPT4", i, m->base4, m->exp4, m->mppt4, &m->err4, &m->fac4);
	}

	// recalc expected with new factors
	mosmix_t avg;
	for (int h = 0; h < 24; h++) {
		ZERO(avg);
		average(&avg, h);
		for (int d = 0; d < 7; d++) {
			mosmix_t *m = HISTORY(d, h);
			expected(m, &avg);
		}
	}

	// recalc errors
	for (int i = 0; i < 24 * 7; i++) {
		mosmix_t *m = &history[i];
		calc("MPPT1", i, m->base1, m->exp1, m->mppt1, &m->err1, &m->fac1);
		calc("MPPT2", i, m->base2, m->exp2, m->mppt2, &m->err2, &m->fac2);
		calc("MPPT3", i, m->base3, m->exp3, m->mppt3, &m->err3, &m->fac3);
		calc("MPPT4", i, m->base4, m->exp4, m->mppt4, &m->err4, &m->fac4);
	}

	// mosmix_store_state();
	mosmix_store_csv();
	mosmix_dump_history_noon();
}

static void test() {
	int itoday, itomorrow, sod, eod, hours, from, to;
	return;

	// define local time object
	struct tm now_tm, *now = &now_tm;
	time_t now_ts = time(NULL);
	localtime_r(&now_ts, now);

	// load state and update forecasts
	mosmix_load_state();
	mosmix_load(MARIENBERG);

	// calculate total daily values
	mosmix_csv_t m0, m1, m2;
	mosmix_24h(0, &m0);
	mosmix_24h(1, &m1);
	mosmix_24h(2, &m2);
	xlog("MOSMIX Rad1h/SunD1/RSunD today %d/%d/%d tomorrow %d/%d/%d tomorrow+1 %d/%d/%d", m0.Rad1h, m0.SunD1, m0.RSunD, m1.Rad1h, m1.SunD1, m1.RSunD, m2.Rad1h, m2.SunD1, m2.RSunD);

	// calculate expected today and tomorrow
	xlog("MOSMIX *** now (%02d) ***", now->tm_hour);
	mosmix_collect(now, &itoday, &itomorrow, &sod, &eod);
	mosmix_dump_today(now);
	mosmix_dump_tomorrow(now);

	xlog("MOSMIX *** updated now (%02d) ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_collect(now, &itoday, &itomorrow, &sod, &eod);
	mosmix_survive(now, 150, &hours, &from, &to);
	mosmix_heating(now, 1500, &hours, &from, &to);

	now->tm_hour = 9;
	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_collect(now, &itoday, &itomorrow, &sod, &eod);
	mosmix_survive(now, 150, &hours, &from, &to);
	mosmix_heating(now, 1500, &hours, &from, &to);

	now->tm_hour = 12;
	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_collect(now, &itoday, &itomorrow, &sod, &eod);
	mosmix_survive(now, 10000, &hours, &from, &to); // -> should be full 24hours
	mosmix_heating(now, 1500, &hours, &from, &to);

	now->tm_hour = 15;
	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_collect(now, &itoday, &itomorrow, &sod, &eod);
	mosmix_survive(now, 150, &hours, &from, &to);
	mosmix_heating(now, 1500, &hours, &from, &to);

	mosmix_dump_today(now);
	mosmix_dump_history_full(now);
	mosmix_dump_history_noon();

	// mosmix_store_state();
}

static void migrate() {
	// return;

	mosmix_old_t old[24 * 7];
	ZERO(old);
	load_blob("/work/fronius-mosmix-history.bin", old, sizeof(old));

	for (int i = 0; i < 24 * 7; i++) {
		mosmix_old_t *o = &old[i];
		mosmix_t *n = &history[i];
		n->Rad1h = o->Rad1h;
		n->SunD1 = o->SunD1;
		n->mppt1 = o->mppt1;
		n->mppt2 = o->mppt2;
		n->mppt3 = o->mppt3;
		n->mppt4 = o->mppt4;
	}

	store_blob(MOSMIX_HISTORY, history, sizeof(history));
}

int mosmix_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	migrate();
	test();
	recalc();

	return 0;
}

#ifdef MOSMIX_MAIN
int main(int argc, char **argv) {
	return mosmix_main(argc, argv);
}
#endif
