#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

#define FLOAT100(x)				((float) x / 100.0)

// all values
static mosmix_file_t mosmix[256];

// 24h slots for today and tomorrow
static mosmix_t mosmix_today[24], mosmix_tomorrow[24];

// gcc -DMOSMIX_MAIN -I ./include/ -o mosmix mosmix.c utils.c

static void copy(mosmix_t *target, mosmix_file_t *source) {
	target->Rad1h = source->Rad1h;
	target->SunD1 = source->SunD1;
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

void mosmix_dump_today() {
	dump_table((int*) mosmix_today, MOSMIX_SIZE, 24, -1, "MOSMIX today", MOSMIX_HEADER);
}

void mosmix_dump_tomorrow() {
	dump_table((int*) mosmix_tomorrow, MOSMIX_SIZE, 24, -1, "MOSMIX tomorrow", MOSMIX_HEADER);
}

void mosmix_takeover() {
	for (int i = 0; i < 24; i++) {
		mosmix_t *m0 = &mosmix_today[i];
		mosmix_t *m1 = &mosmix_tomorrow[i];
		memcpy(m0, m1, sizeof(mosmix_t));
		m0->mppt1 = 0;
		m0->mppt2 = 0;
		m0->mppt3 = 0;
		m0->mppt4 = 0;
	}
}

void mosmix_sum(int *today, int *tomorrow) {
	mosmix_t stoday, stomorrow;
	ZERO(stoday);
	ZERO(stomorrow);
	for (int i = 0; i < 24; i++) {
		sum(&stoday, &mosmix_today[i]);
		sum(&stomorrow, &mosmix_tomorrow[i]);
	}
	*today = stoday.exp1 + stoday.exp2 + stoday.exp3 + stoday.exp4;
	*tomorrow = stomorrow.exp1 + stomorrow.exp2 + stomorrow.exp3 + stomorrow.exp4;
	xdebug("MOSMIX today=%d tomorrow=%d", *today, *tomorrow);
}

void mosmix_update(int hour, int mppt1, int mppt2, int mppt3, int mppt4) {
	float new;

	// today
	mosmix_t *m0 = &mosmix_today[hour];
	m0->x = m0->Rad1h * (1 + (float) m0->SunD1 / 3600 / 2);

	// tomorrow
	mosmix_t *m1 = &mosmix_tomorrow[hour];
	m1->x = m1->Rad1h * (1 + (float) m1->SunD1 / 3600 / 2);

	if (m0->x && mppt1 > 0) {
		m0->mppt1 = mppt1;
		new = (float) m0->mppt1 / (float) m0->x;
		xdebug("MOSMIX MPPT1 factor old %.2f new %.2f", FLOAT100(m0->fac1), new);
		m0->fac1 = new * 100;
	}
	m1->fac1 = m0->fac1;
	m1->exp1 = m1->x * FLOAT100(m1->fac1);

	if (m0->x && mppt2 > 0) {
		m0->mppt2 = mppt2;
		new = (float) m0->mppt2 / (float) m0->x;
		xdebug("MOSMIX MPPT2 factor old %.2f new %.2f", FLOAT100(m0->fac2), new);
		m0->fac2 = new * 100;
	}
	m1->fac2 = m0->fac2;
	m1->exp2 = m1->x * FLOAT100(m1->fac2);

	if (m0->x && mppt3 > 0) {
		m0->mppt3 = mppt3;
		new = (float) m0->mppt3 / (float) m0->x;
		xdebug("MOSMIX MPPT3 factor old %.2f new %.2f", FLOAT100(m0->fac3), new);
		m0->fac3 = new * 100;
	}
	m1->fac3 = m0->fac3;
	m1->exp3 = m1->x * FLOAT100(m1->fac3);

	if (m0->x && mppt4 > 0) {
		m0->mppt4 = mppt4;
		new = (float) m0->mppt4 / (float) m0->x;
		xdebug("MOSMIX MPPT4 factor old %.2f new %.2f", FLOAT100(m0->fac4), new);
		m0->fac4 = new * 100;
	}
	m1->fac4 = m0->fac4;
	m1->exp4 = m1->x * FLOAT100(m1->fac4);
}

void mosmix_sod_eod(int hour, mosmix_t *sod, mosmix_t *eod) {
	ZEROP(sod);
	ZEROP(eod);
	for (int i = 0; i < 24; i++) {
		mosmix_t *m = &mosmix_today[i];
		if (i <= hour)
			sum(sod, m);
		else
			sum(eod, m);
	}
}

// calculate hours to survive next night
void mosmix_survive(time_t now_ts, int rad1h_min, int *hours, int *from, int *to) {
	struct tm tm;
	localtime_r(&now_ts, &tm);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	time_t ts_midnight = mktime(&tm) + 60 * 60 * 24; // last midnight + 1 day

	// find midnight slot
	int midnight;
	for (midnight = 0; midnight < ARRAY_SIZE(mosmix); midnight++)
		if (mosmix[midnight].ts == ts_midnight)
			break;

	// define 24h range: +/-12h around midnight
	int from_index = midnight, to_index = midnight;
	int min = midnight - 12 > 0 ? midnight - 12 : 0;
	int max = midnight + 12 < ARRAY_SIZE(mosmix) ? midnight + 12 : ARRAY_SIZE(mosmix);

	// go backward starting at midnight
	while (--from_index > min)
		if (mosmix[from_index].Rad1h > rad1h_min || mosmix[from_index].ts <= now_ts)
			break; // sundown or 12 noon or now

	// go forward starting at midnight
	while (++to_index < max)
		if (mosmix[to_index].Rad1h > rad1h_min)
			break; // sunrise or 12 noon

	// prepare output
	*hours = to_index - from_index;
	localtime_r(&mosmix[from_index].ts, &tm);
	*from = tm.tm_hour;
	int fRad1h = mosmix[from_index].Rad1h;
	localtime_r(&mosmix[to_index].ts, &tm);
	*to = tm.tm_hour;
	int tRad1h = mosmix[to_index].Rad1h;
	localtime_r(&mosmix[midnight].ts, &tm);
	int mh = tm.tm_hour;
	int mRad1h = mosmix[midnight].Rad1h;
	xdebug("MOSMIX survive hours=%d min=%d from=%d:%d:%d midnight=%d:%d:%d to=%d:%d:%d", *hours, rad1h_min, from_index, *from, fRad1h, midnight, mh, mRad1h, to_index, *to, tRad1h);
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

static mosmix_file_t* current_slot(time_t now_ts) {
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_file_t *m = &mosmix[i];
		if ((m->ts - 3600) < now_ts && now_ts < m->ts)
			return m;
	}
	return 0;
}

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

	// update 24h slots
	localtime_r(&now_ts, &tm);
	int today = tm.tm_mday;
	now_ts += 60 * 60 * 24;
	localtime_r(&now_ts, &tm);
	int tomorrow = tm.tm_mday;
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_file_t *m = &mosmix[i];
		if (m->ts) {
			localtime_r(&m->ts, &tm);
			int d = tm.tm_mday;
			int h = tm.tm_hour;

			mosmix_t *m0 = &mosmix_today[h];
			if (d == today)
				copy(m0, m);

			mosmix_t *m1 = &mosmix_tomorrow[h];
			if (d == tomorrow)
				copy(m1, m);
		}
	}

	return 0;
}

int mosmix_main(int argc, char **argv) {
	int hours, from, to;

	set_xlog(XLOG_STDOUT);
	set_debug(1);

	time_t now_ts = time(NULL);
	mosmix_load(now_ts, CHEMNITZ);

	// find current slot
	mosmix_file_t *m = current_slot(now_ts);
	if (m != 0) {
		char *timestr = ctime(&m->ts);
		timestr[strcspn(timestr, "\n")] = 0; // remove any NEWLINE
		int exp1h = m->Rad1h * 3; // guessed
		xlog("MOSMIX current slot index=%d date=%d (%s) Rad1H=%d SunD1=%d, expected %d Wh", m->idx, m->ts, timestr, m->Rad1h, m->SunD1, exp1h);
	}

	// calculate total daily values
	mosmix_file_t m0, m1, m2;
	mosmix_24h(now_ts, 0, &m0);
	mosmix_24h(now_ts, 1, &m1);
	mosmix_24h(now_ts, 2, &m2);
	xlog("MOSMIX Rad1h/SunD1/RSunD today %d/%d/%d tomorrow %d/%d/%d tomorrow+1 %d/%d/%d", m0.Rad1h, m0.SunD1, m0.RSunD, m1.Rad1h, m1.SunD1, m1.RSunD, m2.Rad1h, m2.SunD1, m2.RSunD);

	// eod - calculate values till end of day
	mosmix_t sod, eod;
	mosmix_sod_eod(now_ts, &sod, &eod);
	xlog("MOSMIX Rad1h/SunD1 sod %d/%d eod %d/%d", sod.Rad1h, sod.SunD1, eod.Rad1h, eod.SunD1);

	// calculate survive time in hours for min Rad1h=100
	mosmix_survive(now_ts, 100, &hours, &from, &to);

	// calculate survive time in hours for min Rad1h=1000 -> should be full 24hours when run before noon
	mosmix_survive(now_ts, 1000, &hours, &from, &to);
	return 0;
}

#ifdef MOSMIX_MAIN
int main(int argc, char **argv) {
	return mosmix_main(argc, argv);
}
#endif
