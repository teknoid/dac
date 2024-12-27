#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

#define SUM_EXP					(m->exp1 + m->exp2 + m->exp3 + m->exp4)
#define SUM_MPPT				(m->mppt1 + m->mppt2 + m->mppt3 + m->mppt4)

// all values
static mosmix_file_t mosmix[256];

// 24h slots for today and tomorrow
static mosmix_t today[24], tomorrow[24];

// gcc -DMOSMIX_MAIN -I ./include/ -o mosmix mosmix.c utils.c

static void copy(mosmix_t *target, mosmix_file_t *source) {
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

void mosmix_takeover() {
	for (int i = 0; i < 24; i++) {
		mosmix_t *m0 = &today[i];
		mosmix_t *m1 = &tomorrow[i];
		memcpy(m0, m1, sizeof(mosmix_t));
		m0->mppt1 = m0->mppt2 = m0->mppt3 = m0->mppt4 = 0;
	}
}

void mosmix_dump_today(int highlight) {
	mosmix_t m;
	cumulate_table((int*) &m, (int*) today, MOSMIX_SIZE, 24);
	dump_table((int*) today, MOSMIX_SIZE, 24, highlight, "MOSMIX today", MOSMIX_HEADER);
	dump_struct((int*) &m, MOSMIX_SIZE, "[++]", 0);
}

void mosmix_dump_tomorrow(int highlight) {
	mosmix_t m;
	cumulate_table((int*) &m, (int*) tomorrow, MOSMIX_SIZE, 24);
	dump_table((int*) tomorrow, MOSMIX_SIZE, 24, highlight, "MOSMIX tomorrow", MOSMIX_HEADER);
	dump_struct((int*) &m, MOSMIX_SIZE, "[++]", 0);
}

void mosmix_mppt(int hour, int mppt1, int mppt2, int mppt3, int mppt4) {
	float new;

	mosmix_t *m0 = &today[hour];
	mosmix_t *m1 = &tomorrow[hour];

	// update todays MPPT values for this hour
	m0->mppt1 = mppt1;
	m0->mppt2 = mppt2;
	m0->mppt3 = mppt3;
	m0->mppt4 = mppt4;

	// recalculate factors in today and take over for tomorrow
	if (m0->x && m0->mppt1) {
		new = (float) m0->mppt1 / (float) m0->x;
		xdebug("MOSMIX hour %d MPPT1 factor old %.2f new %.2f", hour, FLOAT100(m0->fac1), new);
		m0->fac1 = m1->fac1 = new * 100;
	}

	if (m0->x && m0->mppt2) {
		new = (float) m0->mppt2 / (float) m0->x;
		xdebug("MOSMIX hour %d MPPT2 factor old %.2f new %.2f", hour, FLOAT100(m0->fac2), new);
		m0->fac2 = m1->fac2 = new * 100;
	}

	if (m0->x && m0->mppt3) {
		new = (float) m0->mppt3 / (float) m0->x;
		xdebug("MOSMIX hour %d MPPT3 factor old %.2f new %.2f", hour, FLOAT100(m0->fac3), new);
		m0->fac3 = m1->fac3 = new * 100;
	}

	if (m0->x && m0->mppt4) {
		new = (float) m0->mppt4 / (float) m0->x;
		xdebug("MOSMIX hour %d MPPT4 factor old %.2f new %.2f", hour, FLOAT100(m0->fac4), new);
		m0->fac4 = m1->fac4 = new * 100;
	}
}

// calculate total expected today, tomorrow and till end of day / start of day
void mosmix_expected(int hour, int *itoday, int *itomorrow, int *sod, int *eod) {
	mosmix_t sum_today, sum_tomorrow, msod, meod;
	ZERO(sum_today);
	ZERO(sum_tomorrow);
	ZERO(msod);
	ZERO(meod);

	for (int i = 0; i < 24; i++) {
		mosmix_t *htoday = &today[i];
		mosmix_t *htomorrow = &tomorrow[i];
		expected(htoday);
		expected(htomorrow);
		sum(&sum_today, htoday);
		sum(&sum_tomorrow, htomorrow);
		if (i <= hour)
			sum(&msod, htoday);
		else
			sum(&meod, htoday);
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
void mosmix_survive(int hour, int min, int *hours, int *from, int *to) {
	int sundown, sunrise;
	int sundown_expected = 0, sunrise_expected = 0;

	if (hour < 12) {

		// find today sunrise or now
		int sundown = hour;
		for (sunrise = sundown; sunrise < 12; sunrise++) {
			mosmix_t *m = &today[sunrise];
			sunrise_expected = SUM_EXP;
			if (sunrise_expected > min)
				break;
		}
		*hours = sunrise - sundown;
		*from = sundown;
		*to = sunrise;

	} else {

		// find today sundown or now starting backwards from 0:00
		for (sundown = 23; sundown >= 12 && sundown > hour; sundown--) {
			mosmix_t *m = &today[sundown];
			sundown_expected = SUM_EXP;
			if (sundown_expected > min)
				break;
		}

		// find tomorrow sunrise
		for (sunrise = 0; sunrise < 12; sunrise++) {
			mosmix_t *m = &tomorrow[sunrise];
			sunrise_expected = SUM_EXP;
			if (sunrise_expected > min)
				break;
		}
		*hours = 24 - sundown + sunrise;
		*from = sundown;
		*to = sunrise;
	}

	xdebug("MOSMIX survive hours=%d min=%d from=%d/%d to=%d/%d", *hours, min, sundown, sundown_expected, sunrise, sunrise_expected);
}

// sum up 24 mosmix slots for one day (with offset)
void mosmix_24h(time_t now_ts, int day, mosmix_file_t *sum) {
	struct tm tm;

	// calculate today 0:00:00 as start and +24h as end time frame
	localtime_r(&now_ts, &tm);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	time_t ts_from = mktime(&tm) + 60 * 60 * 24 * day;
	time_t ts_to = ts_from + 60 * 60 * 24; // + 1 day

	ZEROP(sum);
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_file_t *m = &mosmix[i];
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
	mosmix_file_t *m = &mosmix[idx];

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
	struct tm tm;

	ZEROP(mosmix);

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

	// update 24h today and tomorrow slots
	localtime_r(&now_ts, &tm);
	int dtoday = tm.tm_mday;
	now_ts += 60 * 60 * 24;
	localtime_r(&now_ts, &tm);
	int dtomorrow = tm.tm_mday;
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_file_t *m = &mosmix[i];
		if (m->ts) {
			time_t t = m->ts - 1; // fix hour
			localtime_r(&t, &tm);
			int h = tm.tm_hour;
			int d = tm.tm_mday;
			if (d == dtoday)
				copy(&today[h], m);
			if (d == dtomorrow)
				copy(&tomorrow[h], m);
		}
	}

	return 0;
}

void mosmix_load_state() {
	ZEROP(today);
	ZEROP(tomorrow);
	load_blob(TODAY_FILE, today, sizeof(today));
	load_blob(TOMORROW_FILE, tomorrow, sizeof(tomorrow));
}

void mosmix_store_state() {
	store_blob(TODAY_FILE, today, sizeof(today));
	store_blob(TOMORROW_FILE, tomorrow, sizeof(tomorrow));
}

int mosmix_main(int argc, char **argv) {
	int today, tomorrow, sod, eod, hours, from, to;
	struct tm tm;

	set_xlog(XLOG_STDOUT);
	set_debug(1);

	time_t now_ts = time(NULL);
	localtime_r(&now_ts, &tm);

	// load state and update forecasts
	mosmix_load_state();
	mosmix_load(now_ts, MARIENBERG);

	// calculate expected
	mosmix_expected(tm.tm_hour, &today, &tomorrow, &sod, &eod);

	// dump
	mosmix_dump_today(tm.tm_hour);
	mosmix_dump_tomorrow(tm.tm_hour);

	// calculate total daily values
	mosmix_file_t m0, m1, m2;
	mosmix_24h(now_ts, 0, &m0);
	mosmix_24h(now_ts, 1, &m1);
	mosmix_24h(now_ts, 2, &m2);
	xlog("MOSMIX Rad1h/SunD1/RSunD today %d/%d/%d tomorrow %d/%d/%d tomorrow+1 %d/%d/%d", m0.Rad1h, m0.SunD1, m0.RSunD, m1.Rad1h, m1.SunD1, m1.RSunD, m2.Rad1h, m2.SunD1, m2.RSunD);

	mosmix_survive(tm.tm_hour, 150, &hours, &from, &to);
	// mosmix_survive(17, 150, &hours, &from, &to);
	mosmix_survive(12, 10000, &hours, &from, &to); // -> should be full 24hours

	return 0;
}

#ifdef MOSMIX_MAIN
int main(int argc, char **argv) {
	return mosmix_main(argc, argv);
}
#endif
