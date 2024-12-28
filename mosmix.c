#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

// gcc -DMOSMIX_MAIN -I ./include/ -o mosmix mosmix.c utils.c

#define SUM_EXP					(m->exp1 + m->exp2 + m->exp3 + m->exp4)
#define SUM_MPPT				(m->mppt1 + m->mppt2 + m->mppt3 + m->mppt4)

// all values
static mosmix_csv_t mosmix_csv[256];

// 24h slots over one week and access pointers
static mosmix_t mosmix_hours[24 * 7];
#define MOSMIX_NOW				(&mosmix_hours[24 * now->tm_wday + now->tm_hour])
#define MOSMIX_TODAY(h)			(&mosmix_hours[24 * now->tm_wday + h])
#define MOSMIX_TOMORROW(h)		(&mosmix_hours[24 * (now->tm_wday < 6 ? now->tm_wday + 1 : 0) + h])

static void copy(mosmix_t *target, mosmix_csv_t *source) {
	target->Rad1h = source->Rad1h;
	target->SunD1 = source->SunD1;

	// calculate base value as a combination of Rad1h and SunD1
	// target->x = target->Rad1h * (1 + (float) target->SunD1 / 3600 / 2);
	// target->x = target->Rad1h + target->SunD1 / 10;
	target->x = target->Rad1h;
}

static void sum(mosmix_t *s, mosmix_t *m) {
	s->Rad1h += m->Rad1h;
	s->SunD1 += m->SunD1;
	s->x += m->x;

	s->mppt1 += m->mppt1;
	s->mppt2 += m->mppt2;
	s->mppt3 += m->mppt3;
	s->mppt4 += m->mppt4;

	s->exp1 += m->exp1;
	s->exp2 += m->exp2;
	s->exp3 += m->exp3;
	s->exp4 += m->exp4;
}

static void expected(mosmix_t *m) {
	m->exp1 = m->x * FLOAT100(m->fac1);
	m->exp2 = m->x * FLOAT100(m->fac2);
	m->exp3 = m->x * FLOAT100(m->fac3);
	m->exp4 = m->x * FLOAT100(m->fac4);
}

static void mppt(const char *id, int hour, int *x, int *mppt, int *fact0, int *fact1) {
	float old = FLOAT100(*fact0);
	float act = *x ? (float) *mppt / (float) *x : 0;
	float new = old ? (old + act) / 2.0 : act;
	xdebug("MOSMIX %s hour %d factor old %.2f actual %.2f new %.2f", id, hour, old, act, new);
	*fact0 = *fact1 = new * 100; // store as x100 scaled
}

void mosmix_dump_today(struct tm *now) {
	mosmix_t m;
	cumulate_table((int*) &m, (int*) MOSMIX_TODAY(0), MOSMIX_SIZE, 24);
	dump_table((int*) MOSMIX_TODAY(0), MOSMIX_SIZE, 24, now->tm_hour, "MOSMIX today", MOSMIX_HEADER);
	dump_struct((int*) &m, MOSMIX_SIZE, "[++]", 0);
}

void mosmix_dump_tomorrow(struct tm *now) {
	mosmix_t m;
	cumulate_table((int*) &m, (int*) MOSMIX_TOMORROW(0), MOSMIX_SIZE, 24);
	dump_table((int*) MOSMIX_TOMORROW(0), MOSMIX_SIZE, 24, now->tm_hour, "MOSMIX tomorrow", MOSMIX_HEADER);
	dump_struct((int*) &m, MOSMIX_SIZE, "[++]", 0);
}

void mosmix_mppt(struct tm *now, int mppt1, int mppt2, int mppt3, int mppt4) {
	mosmix_t *m0 = MOSMIX_TODAY(now->tm_hour);
	mosmix_t *m1 = MOSMIX_TOMORROW(now->tm_hour);

	// update todays MPPT values for this hour
	m0->mppt1 = mppt1;
	m0->mppt2 = mppt2;
	m0->mppt3 = mppt3;
	m0->mppt4 = mppt4;

	// recalculate mosmix factors for each mppt
	mppt("MPPT1", now->tm_hour, &m0->x, &m0->mppt1, &m0->fac1, &m1->fac1);
	mppt("MPPT2", now->tm_hour, &m0->x, &m0->mppt2, &m0->fac2, &m1->fac2);
	mppt("MPPT3", now->tm_hour, &m0->x, &m0->mppt3, &m0->fac3, &m1->fac3);
	mppt("MPPT4", now->tm_hour, &m0->x, &m0->mppt4, &m0->fac4, &m1->fac4);
}

// calculate total expected today, tomorrow and till end of day / start of day
void mosmix_expected(struct tm *now, int *itoday, int *itomorrow, int *sod, int *eod) {
	mosmix_t sum_today, sum_tomorrow, msod, meod;
	ZERO(sum_today);
	ZERO(sum_tomorrow);
	ZERO(msod);
	ZERO(meod);

	for (int h = 0; h < 24; h++) {
		mosmix_t *m0 = MOSMIX_TODAY(h);
		mosmix_t *m1 = MOSMIX_TOMORROW(h);
		expected(m0);
		expected(m1);
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
	xdebug("MOSMIX today=%d tomorrow=%d sod=%d eod=%d", *itoday, *itomorrow, *sod, *eod);
	if (*itoday != *sod + *eod) // validate
		xdebug("MOSMIX sod/eod calculation error %d != %d + %d", *itoday, *sod, *eod);
}

// calculate hours to survive next night
void mosmix_survive(struct tm *now, int min, int *hours, int *from, int *to) {
	int h, from_expected = 0, to_expected = 0;

	// find today sundown or now starting backwards from 0:00
	for (h = 23; h >= 12 && h > now->tm_hour; h--) {
		mosmix_t *m = MOSMIX_TODAY(h);
		from_expected = SUM_EXP;
		if (from_expected > min)
			break;
	}
	*from = h;

	// find tomorrow sunrise
	for (h = 0; h < 12; h++) {
		mosmix_t *m = MOSMIX_TOMORROW(h);
		to_expected = SUM_EXP;
		if (to_expected > min)
			break;
	}
	*to = h;

	*hours = 24 - *from + *to;
	xdebug("MOSMIX survive hours=%d min=%d from=%d/%d to=%d/%d", *hours, min, *from, from_expected, *to, to_expected);
}

// calculate hours where we have enough power for heating
void mosmix_heating(struct tm *now, int min, int *hours, int *from, int *to) {
	int h, from_expected = 0, to_expected = 0;

	// find first hour with enough expected
	for (h = now->tm_hour; h < 24; h++) {
		mosmix_t *m = MOSMIX_TODAY(h);
		from_expected = SUM_EXP;
		if (from_expected > min)
			break;
	}
	*from = h;

	// find last hour with enough expected
	for (h = 23; h >= now->tm_hour; h--) {
		mosmix_t *m = MOSMIX_TODAY(h);
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

	xdebug("MOSMIX heating hours=%d min=%d from=%d/%d to=%d/%d", *hours, min, *from, from_expected, *to, to_expected);
}

// sum up 24 mosmix slots for one day (with offset)
void mosmix_24h(time_t now_ts, int day, mosmix_csv_t *sum) {
	struct tm tm;

	// calculate today 0:00:00 as start and +24h as end time frame
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

int mosmix_load(time_t now_ts, const char *filename) {
	char *strings[MOSMIX_COLUMNS];
	char buf[LINEBUF];

	ZEROP(mosmix_csv);

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
		while (p != NULL) {
			p[strcspn(p, "\n")] = 0; // remove any NEWLINE
			strings[i++] = p;
			p = strtok(NULL, ",");
		}
		parse(strings, ARRAY_SIZE(strings));
		lines++;
	}

	fclose(fp);
	xlog("MOSMIX loaded %s containing %d lines", filename, lines);

	// update mosmix slots - maximal 24h, 7 days - mosmix forecast contains more(!)
	struct tm now_tm, *now = &now_tm;
	for (int i = 0; i < 24 * 7; i++) {
		mosmix_csv_t *m = &mosmix_csv[i];
		if (m->ts) {
			time_t t = m->ts - 1; // fix hour
			localtime_r(&t, now);
			copy(MOSMIX_NOW, m);
		}
	}

	return 0;
}

void mosmix_load_state() {
	ZERO(mosmix_hours);
	load_blob(MOSMIX_FILE, mosmix_hours, sizeof(mosmix_hours));
}

void mosmix_store_state() {
	store_blob(MOSMIX_FILE, mosmix_hours, sizeof(mosmix_hours));
}

int mosmix_main(int argc, char **argv) {
	int today, tomorrow, sod, eod, hours, from, to;

	set_xlog(XLOG_STDOUT);
	set_debug(1);

	// define local time object
	struct tm now_tm, *now = &now_tm;
	time_t now_ts = time(NULL);
	localtime_r(&now_ts, now);

	// load state and update forecasts
	mosmix_load_state();
	mosmix_load(now_ts, MARIENBERG);
	mosmix_dump_today(now);
	mosmix_dump_tomorrow(now);

	// update mppts
	mosmix_mppt(now, 4000, 3000, 2000, 1000);

	// calculate expected
	mosmix_expected(now, &today, &tomorrow, &sod, &eod);

	// calculate total daily values
	mosmix_csv_t m0, m1, m2;
	mosmix_24h(now_ts, 0, &m0);
	mosmix_24h(now_ts, 1, &m1);
	mosmix_24h(now_ts, 2, &m2);
	xlog("MOSMIX Rad1h/SunD1/RSunD today %d/%d/%d tomorrow %d/%d/%d tomorrow+1 %d/%d/%d", m0.Rad1h, m0.SunD1, m0.RSunD, m1.Rad1h, m1.SunD1, m1.RSunD, m2.Rad1h, m2.SunD1, m2.RSunD);

	mosmix_survive(now, 150, &hours, &from, &to);
	mosmix_heating(now, 1500, &hours, &from, &to);

	now->tm_hour = 9;
	mosmix_survive(now, 150, &hours, &from, &to);
	mosmix_heating(now, 1500, &hours, &from, &to);
	now->tm_hour = 12;
	mosmix_survive(now, 10000, &hours, &from, &to); // -> should be full 24hours
	mosmix_heating(now, 1500, &hours, &from, &to);
	now->tm_hour = 15;
	mosmix_survive(now, 1500, &hours, &from, &to);
	mosmix_heating(now, 1500, &hours, &from, &to);

	// mosmix_store_state();
	return 0;
}

#ifdef MOSMIX_MAIN
int main(int argc, char **argv) {
	return mosmix_main(argc, argv);
}
#endif
