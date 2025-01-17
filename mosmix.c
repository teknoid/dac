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

// all values
static mosmix_csv_t mosmix_csv[256];

// 24h slots over one week and access pointers
// today and tommorow contains only forecast data (base+expected)
// history contains all data (forecast, mppt, factors and errors)
static mosmix_t today[24], tomorrow[24], history[24 * 7];
#define TODAY(h)				(&today[h])
#define TOMORROW(h)				(&tomorrow[h])
#define HISTORY(d, h)			(&history[24 * d + h])

static void scale1(int factor) {
	float f = 1.0 + FLOAT100(factor);
	xlog("MOSMIX scaling today's expected MPPT1 values by %5.2f", f);
	for (int i = 0; i < 24; i++)
		today[i].exp1 *= f;
}

static void scale2(int factor) {
	float f = 1.0 + FLOAT100(factor);
	xlog("MOSMIX scaling today's expected MPPT2 values by %5.2f", f);
	for (int i = 0; i < 24; i++)
		today[i].exp2 *= f;
}

static void scale3(int factor) {
	float f = 1.0 + FLOAT100(factor);
	xlog("MOSMIX scaling today's expected MPPT3 values by %5.2f", f);
	for (int i = 0; i < 24; i++)
		today[i].exp3 *= f;
}

static void scale4(int factor) {
	float f = 1.0 + FLOAT100(factor);
	xlog("MOSMIX scaling today's expected MPPT4 values by %5.2f", f);
	for (int i = 0; i < 24; i++)
		today[i].exp4 *= f;
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

static void expected(mosmix_t *m, mosmix_t *fcts) {
	m->exp1 = m->base * (1.0 + FLOAT100(fcts->fac1));
	m->exp2 = m->base * (1.0 + FLOAT100(fcts->fac2));
	m->exp3 = m->base * (1.0 + FLOAT100(fcts->fac3));
	m->exp4 = m->base * (1.0 + FLOAT100(fcts->fac4));
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
	avg->fac1 = counts.fac1 ? avg->fac1 / counts.fac1 : 0;
	avg->fac2 = counts.fac2 ? avg->fac2 / counts.fac2 : 0;
	avg->fac3 = counts.fac3 ? avg->fac3 / counts.fac3 : 0;
	avg->fac4 = counts.fac4 ? avg->fac4 / counts.fac4 : 0;
	if (avg->fac1)
		xdebug("MOSMIX hour %02d average factors %5.2f %5.2f %5.2f %5.2f", h, FLOAT100(avg->fac1), FLOAT100(avg->fac2), FLOAT100(avg->fac3), FLOAT100(avg->fac4));
}

static void update_today_tomorrow() {
	// get todays and tomorrows year day
	struct tm tm;
	time_t t = time(NULL);
	localtime_r(&t, &tm);
	int day_today = tm.tm_yday;
	int day_tomorrow = tm.tm_yday != 365 ? tm.tm_yday + 1 : 0;

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

		// calculate base value as a combination of Rad1h / SunD1 / TTT etc.
		m->base = m->Rad1h * (1 + (float) m->SunD1 / 3600);
		// m->base = m->Rad1h + m->SunD1 / 10;
		// m->base = m->Rad1h;

		if (m->Rad1h)
			xdebug("MOSMIX updated %02d.%02d. hour %02d Rad1h=%d SunD1=%d base=%d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, m->Rad1h, m->SunD1, m->base);
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
	// error expected vs. actual
	float error = exp ? (float) mppt / (float) exp - 1.0 : 0.0;
	*err = error * 100; // store as x100 scaled
	// new factor
	float old = FLOAT100(*fac);
	float new = base ? (float) mppt / (float) base - 1.0 : 0.0;
	xdebug("MOSMIX %s hour %02d   expected %4d actual %4d error %5.2f   factor old %5.2f new %5.2f", id, hour, exp, mppt, error, old, new);
	*fac = new * 100; // store as x100 scaled
}

void mosmix_mppt(struct tm *now, int mppt1, int mppt2, int mppt3, int mppt4) {
	mosmix_t *m = TODAY(now->tm_hour);
	mosmix_t *h = HISTORY(now->tm_wday, now->tm_hour);
	mosmix_t *h1 = HISTORY(now->tm_wday, now->tm_hour > 00 ? now->tm_hour - 1 : 23);

	// copy to history
	memcpy(h, m, sizeof(mosmix_t));

	// update actual pv
	h->mppt1 = mppt1;
	h->mppt2 = mppt2;
	h->mppt3 = mppt3;
	h->mppt4 = mppt4;

	// recalculate
	calc("MPPT1", now->tm_hour, h->base, h->exp1, h->mppt1, &h->err1, &h->fac1);
	calc("MPPT2", now->tm_hour, h->base, h->exp2, h->mppt2, &h->err2, &h->fac2);
	calc("MPPT3", now->tm_hour, h->base, h->exp3, h->mppt3, &h->err3, &h->fac3);
	calc("MPPT4", now->tm_hour, h->base, h->exp4, h->mppt4, &h->err4, &h->fac4);

	// validate today's forecast - if this and last errors are greater than 10% scale all values
	int e1 = (h->err1 < -10 && h1->err1 < -10) || (h->err1 > 10 && h1->err1 > 10);
	int e2 = (h->err2 < -10 && h1->err2 < -10) || (h->err2 > 10 && h1->err2 > 10);
	int e3 = (h->err3 < -10 && h1->err3 < -10) || (h->err3 > 10 && h1->err3 > 10);
	int e4 = (h->err4 < -10 && h1->err4 < -10) || (h->err4 > 10 && h1->err4 > 10);
	if (e1)
		scale1((h->err1 + h1->err1) / 2);
	if (e2)
		scale2((h->err2 + h1->err2) / 2);
	if (e3)
		scale3((h->err3 + h1->err3) / 2);
	if (e4)
		scale4((h->err4 + h1->err4) / 2);
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
	*from = h;

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
		*hours = *to = *from = 0;

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

	ZERO(today);
	ZERO(tomorrow);
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

static void fix() {
//	ZERO(history);
//	load_blob(MOSMIX_HISTORY, history, sizeof(history));
//	for (int i = 0; i < 24 * 7; i++) {
//		mosmix_t *m = &history[i];
//		// recalculate
//		calc("MPPT1", i, m->base, m->exp1, m->mppt1, &m->err1, &m->fac1);
//		calc("MPPT2", i, m->base, m->exp2, m->mppt2, &m->err2, &m->fac2);
//		calc("MPPT3", i, m->base, m->exp3, m->mppt3, &m->err3, &m->fac3);
//		calc("MPPT4", i, m->base, m->exp4, m->mppt4, &m->err4, &m->fac4);
//	}
//	store_blob(MOSMIX_HISTORY, history, sizeof(history));
//	store_csv((int*) history, MOSMIX_SIZE, 24 * 7, MOSMIX_HEADER, MOSMIX_HISTORY_CSV);
}

static void test() {
	int itoday, itomorrow, sod, eod, hours, from, to;

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

	mosmix_dump_history_full(now);
	mosmix_dump_history_noon();

	// mosmix_store_state();
}

int mosmix_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	test();
	fix();

	return 0;
}

#ifdef MOSMIX_MAIN
int main(int argc, char **argv) {
	return mosmix_main(argc, argv);
}
#endif
