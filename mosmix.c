#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

// gcc -Wall -DMOSMIX_MAIN -I ./include/ -o mosmix mosmix.c utils.c

#define SUM_EXP					(m->exp1 + m->exp2 + m->exp3 + m->exp4)
#define SUM_MPPT				(m->mppt1 + m->mppt2 + m->mppt3 + m->mppt4)

#define FACTORS_MAX				333

// all values
static mosmix_csv_t mosmix_csv[256];

// 24h slots over one week and access pointers
// today and tommorow contains only forecast data
// history contains all data (forecast, mppt, factors and errors)
static mosmix_t today[24], tomorrow[24], history[24 * 7];
#define TODAY(h)				(&today[h])
#define TOMORROW(h)				(&tomorrow[h])
#define HISTORY(d, h)			(&history[24 * d + h])

// rad1/sund1 coefficients per MPPT and hour and access pointer
static factor_t factors[24];
#define FACTORS(h)				(&factors[h])

static void scale1(struct tm *now, mosmix_t *m) {
	if (90 < m->err1 && m->err1 < 110)
		return;
	for (int h = now->tm_hour + 1; h < 24; h++)
		TODAY(h)->exp1 = TODAY(h)->exp1 * m->err1 / 100;
	xlog("MOSMIX scaling today's remaining MPPT1 forecasts by %5.2f", FLOAT100(m->err1));
}

static void scale2(struct tm *now, mosmix_t *m) {
	if (90 < m->err2 && m->err2 < 110)
		return;
	for (int h = now->tm_hour + 1; h < 24; h++)
		TODAY(h)->exp2 = TODAY(h)->exp2 * m->err2 / 100;
	xlog("MOSMIX scaling today's remaining MPPT2 forecasts by %5.2f", FLOAT100(m->err2));
}

static void scale3(struct tm *now, mosmix_t *m) {
	if (90 < m->err3 && m->err3 < 110)
		return;
	for (int h = now->tm_hour + 1; h < 24; h++)
		TODAY(h)->exp3 = TODAY(h)->exp3 * m->err3 / 100;
	xlog("MOSMIX scaling today's remaining MPPT3 forecasts by %5.2f", FLOAT100(m->err3));
}

static void scale4(struct tm *now, mosmix_t *m) {
	if (90 < m->err4 && m->err4 < 110)
		return;
	for (int h = now->tm_hour + 1; h < 24; h++)
		TODAY(h)->exp4 = TODAY(h)->exp4 * m->err4 / 100;
	xlog("MOSMIX scaling today's remaining MPPT4 forecasts by %5.2f", FLOAT100(m->err4));
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

// calculate expected pv as combination of raw mosmix values with mppt specific coefficients
static void expected(mosmix_t *m, factor_t *f) {
	m->exp1 = f->r1 * m->Rad1h / 100 + f->s1 * m->SunD1 / 100;
	m->exp2 = f->r2 * m->Rad1h / 100 + f->s2 * m->SunD1 / 100;
	m->exp3 = f->r3 * m->Rad1h / 100 + f->s3 * m->SunD1 / 100;
	m->exp4 = f->r4 * m->Rad1h / 100 + f->s4 * m->SunD1 / 100;
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

// initially fill today and tomorrow from history
static void update_today_tomorrow_from_history(struct tm *now) {
	int day_today = now->tm_wday;
	int day_tomorrow = day_today < 6 ? day_today + 1 : 0;
	for (int h = 0; h < 24; h++) {
		memcpy(TODAY(h), HISTORY(day_today, h), sizeof(mosmix_t));
		memcpy(TOMORROW(h), HISTORY(day_tomorrow, h), sizeof(mosmix_t));
	}
}

// update today and tomorrow with actual data from mosmix kml download
static void update_today_tomorrow(struct tm *now) {
	int day_today = now->tm_yday;
	int day_tomorrow = now->tm_yday < 365 ? now->tm_yday + 1 : 0;

	// loop over one week
	for (int i = 0; i < 24 * 7; i++) {
		mosmix_csv_t *mcsv = &mosmix_csv[i];

		// find mosmix slot to update
		struct tm tm;
		time_t t = mcsv->ts - 1; // fix hour
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
	}

	// calculate each mppt's forecast today and tomorrow
	for (int h = 0; h < 24; h++) {
		factor_t *f = FACTORS(h);
		mosmix_t *m0 = TODAY(h);
		mosmix_t *m1 = TOMORROW(h);
		expected(m0, f);
		expected(m1, f);
	}
}

// brute force coefficients determination per MPPT and hour
void mosmix_factors() {
	ZERO(factors);

	for (int h = 0; h < 24; h++) {
		factor_t *f = FACTORS(h);

		f->e1 = f->e2 = f->e3 = f->e4 = INT16_MAX;
		for (int r = 0; r < FACTORS_MAX; r++) {
			for (int s = 0; s < FACTORS_MAX; s++) {
				mosmix_t cum, x;
				ZERO(cum);

				for (int d = 0; d < 7; d++) {
					mosmix_t *m = HISTORY(d, h);

					// calculate expected
					x.exp1 = r * m->Rad1h / 100 + s * m->SunD1 / 100;
					x.exp2 = r * m->Rad1h / 100 + s * m->SunD1 / 100;
					x.exp3 = r * m->Rad1h / 100 + s * m->SunD1 / 100;
					x.exp4 = r * m->Rad1h / 100 + s * m->SunD1 / 100;

					// calculate absolute error
					x.err1 = m->mppt1 > x.exp1 ? m->mppt1 - x.exp1 : x.exp1 - m->mppt1;
					x.err2 = m->mppt2 > x.exp2 ? m->mppt2 - x.exp2 : x.exp2 - m->mppt2;
					x.err3 = m->mppt3 > x.exp3 ? m->mppt3 - x.exp3 : x.exp3 - m->mppt3;
					x.err4 = m->mppt4 > x.exp4 ? m->mppt4 - x.exp4 : x.exp4 - m->mppt4;

					// sum up errors
					cum.err1 += x.err1;
					cum.err2 += x.err2;
					cum.err3 += x.err3;
					cum.err4 += x.err4;
				}

				// now find the coefficients producing the smallest error and store them to table
				if (cum.err1 < f->e1) {
					f->r1 = r;
					f->s1 = s;
					f->e1 = cum.err1;
				}
				if (cum.err2 < f->e2) {
					f->r2 = r;
					f->s2 = s;
					f->e2 = cum.err2;
				}
				if (cum.err3 < f->e3) {
					f->r3 = r;
					f->s3 = s;
					f->e3 = cum.err3;
				}
				if (cum.err4 < f->e4) {
					f->r4 = r;
					f->s4 = s;
					f->e4 = cum.err4;
				}
			}
		}
	}
	dump_table((int*) factors, FACTOR_SIZE, 24, 0, "MOSMIX factors", FACTOR_HEADER);
}

void mosmix_mppt(struct tm *now, int mppt1, int mppt2, int mppt3, int mppt4) {
	mosmix_t *m = TODAY(now->tm_hour);

	// update actual pv
	m->mppt1 = mppt1;
	m->mppt2 = mppt2;
	m->mppt3 = mppt3;
	m->mppt4 = mppt4;

	// calculate errors as actual - expected
	m->diff1 = m->mppt1 - m->exp1;
	m->diff2 = m->mppt2 - m->exp2;
	m->diff3 = m->mppt3 - m->exp3;
	m->diff4 = m->mppt4 - m->exp4;

	// calculate errors as actual / expected
	m->err1 = m->exp1 ? m->mppt1 * 100 / m->exp1 : 100;
	m->err2 = m->exp2 ? m->mppt2 * 100 / m->exp2 : 100;
	m->err3 = m->exp3 ? m->mppt3 * 100 / m->exp3 : 100;
	m->err4 = m->exp4 ? m->mppt4 * 100 / m->exp4 : 100;

	// save to history
	mosmix_t *mh = HISTORY(now->tm_wday, now->tm_hour);
	memcpy(mh, m, sizeof(mosmix_t));

	xdebug("MOSMIX forecast errors Wh %5d %5d %5d %5d sum %d", m->diff1, m->diff2, m->diff3, m->diff4, m->diff1 + m->diff2 + m->diff3 + m->diff4);
	xdebug("MOSMIX forecast errors  %% %5.2f %5.2f %5.2f %5.2f", FLOAT100(m->err1), FLOAT100(m->err2), FLOAT100(m->err3), FLOAT100(m->err4));

	// validate today's forecast - if error is greater than +/- 10% scale all remaining values
	scale1(now, m);
	scale2(now, m);
	scale3(now, m);
	scale4(now, m);
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

void mosmix_dump_history_hours(int h) {
	xlog("MOSMIX history hours\n    "MOSMIX_HEADER);
	char idx[6];
	snprintf(idx, 6, "[%02d]", h);
	for (int d = 0; d < 7; d++)
		dump_struct((int*) HISTORY(d, h), MOSMIX_SIZE, idx, 0);
}

void mosmix_clear_today_tomorrow() {
	ZERO(today);
	ZERO(tomorrow);
}

void mosmix_load_history(struct tm *now) {
	ZERO(history);
	load_blob(MOSMIX_HISTORY, history, sizeof(history));
	mosmix_clear_today_tomorrow();
	update_today_tomorrow_from_history(now);
}

void mosmix_store_history() {
	store_blob(MOSMIX_HISTORY, history, sizeof(history));
}
void mosmix_store_csv() {
	store_csv((int*) history, MOSMIX_SIZE, 24 * 7, MOSMIX_HEADER, MOSMIX_HISTORY_CSV);
	store_csv((int*) factors, FACTOR_SIZE, 24, FACTOR_HEADER, MOSMIX_FACTORS_CSV);
	store_csv((int*) today, MOSMIX_SIZE, 24, MOSMIX_HEADER, MOSMIX_TODAY_CSV);
	store_csv((int*) tomorrow, MOSMIX_SIZE, 24, MOSMIX_HEADER, MOSMIX_TOMORROW_CSV);
}

int mosmix_load(struct tm *now, const char *filename) {
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

	update_today_tomorrow(now);
	return 0;
}

static void recalc() {
	return;

	ZERO(history);
	load_blob(MOSMIX_HISTORY, history, sizeof(history));
	mosmix_factors();

	// recalc expected with new factors
	for (int d = 0; d < 7; d++) {
		for (int h = 0; h < 24; h++) {
			mosmix_t *m = HISTORY(d, h);
			factor_t *f = FACTORS(h);
			expected(m, f);
		}
	}

	// recalc errors
	for (int i = 0; i < 24 * 7; i++) {
		mosmix_t *m = &history[i];

		m->diff1 = m->mppt1 - m->exp1;
		m->diff2 = m->mppt2 - m->exp2;
		m->diff3 = m->mppt3 - m->exp3;
		m->diff4 = m->mppt4 - m->exp4;

		m->err1 = m->exp1 ? m->mppt1 * 100 / m->exp1 : 100;
		m->err2 = m->exp2 ? m->mppt2 * 100 / m->exp2 : 100;
		m->err3 = m->exp3 ? m->mppt3 * 100 / m->exp3 : 100;
		m->err4 = m->exp4 ? m->mppt4 * 100 / m->exp4 : 100;
	}

	// mosmix_store_state();
	mosmix_store_csv();
	mosmix_dump_history_hours(12);
}

static void test() {
	int itoday, itomorrow, sod, eod, hours, from, to;
	return;

	// define local time object
	struct tm now_tm, *now = &now_tm;
	time_t now_ts = time(NULL);
	localtime_r(&now_ts, now);

	// load state and update forecasts
	mosmix_load_history(now);
	mosmix_factors();
	mosmix_load(now, MARIENBERG);

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
	mosmix_dump_history_hours(9);
	mosmix_dump_history_hours(12);
	mosmix_dump_history_hours(15);

	// mosmix_store_state();
}

static void migrate() {
	return;

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

static void wget(struct tm *now, const char *id) {
	char ftstamp[32], fname[64], fforecasts[64], ftimestamps[64], furl[256], cmd[288];
	chdir("/tmp");

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

static int diffs(int d) {
	int diff_sum = 0;
	for (int h = 0; h < 24; h++) {
		mosmix_t *mh = HISTORY(d, h);
		mosmix_t *m = TODAY(h);
		int pv = mh->mppt1 + mh->mppt2 + mh->mppt3 + mh->mppt4;
		int diff = pv - m->Rad1h - m->SunD1;
		diff_sum += abs(diff);
		if (diff)
			printf("hour %02d mppt1 %4d Rad1H %4d SunD1 %4d --> err %4d\n", h, pv, m->Rad1h, m->SunD1, diff);
	}
	return diff_sum;
}

static void compare() {
	// define local time object
	struct tm now_tm, *now = &now_tm;
	time_t now_ts = time(NULL);
	localtime_r(&now_ts, now);

	mosmix_load_history(now);

	wget(now, "10577");
	mosmix_load(now, CHEMNITZ);
	int dc = diffs(now->tm_wday);

	wget(now, "10579");
	mosmix_load(now, MARIENBERG);
	int dm = diffs(now->tm_wday);

	printf("%4d-%02d-%02d Diffs:   Chemnitz %d   Marienberg %d\n", now->tm_year + 1900, now->tm_mon + 1, now->tm_mday, dc, dm);
}

int mosmix_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	migrate();
	test();
	recalc();
	compare();

	return 0;
}

#ifdef MOSMIX_MAIN
int main(int argc, char **argv) {
	return mosmix_main(argc, argv);
}
#endif
