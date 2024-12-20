#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

static mosmix_t mosmix[256], mosmix24[24];

// gcc -DMOSMIX_MAIN -I ./include/ -o mosmix mosmix.c utils.c

static void parse(char **strings, size_t size) {
	int idx = atoi(strings[0]);
	mosmix_t *m = &mosmix[idx];

	m->idx = idx;
	m->ts = atoi(strings[1]);
	m->TTT = atof(strings[2]) - 273.15;
	m->Rad1h = atoi(strings[3]);
	m->SunD1 = atoi(strings[4]);
}

void mosmix_24h(time_t now_ts, int day, mosmix_t *sum) {
	struct tm tm;

	// calculate today 0:00:00 as start and +24h as end time frame
	localtime_r(&now_ts, &tm);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	time_t ts_from = mktime(&tm) + 60 * 60 * 24 * day;
	time_t ts_to = ts_from + 60 * 60 * 24; // + 1 day

	ZEROP(sum);
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_t *m = &mosmix[i];
		if (ts_from < m->ts && m->ts <= ts_to) {
			sum->Rad1h += m->Rad1h;
			sum->SunD1 += m->SunD1;
		}
	}
}

void mosmix_sod_eod(time_t now_ts, mosmix_t *sod, mosmix_t *eod) {
	ZEROP(sod);
	ZEROP(eod);
	for (int i = 0; i < 24; i++) {
		mosmix_t *m = &mosmix24[i];
		if (m->ts <= now_ts) {
			sod->Rad1h += m->Rad1h;
			sod->SunD1 += m->SunD1;
		} else {
			eod->Rad1h += m->Rad1h;
			eod->SunD1 += m->SunD1;
		}
	}
}

float mosmix_noon() {
	mosmix_t forenoon, afternoon;

	ZERO(forenoon);
	ZERO(afternoon);
	for (int i = 0; i < 24; i++) {
		mosmix_t *m = &mosmix24[i];
		if (i <= 12) {
			forenoon.Rad1h += m->Rad1h;
			forenoon.SunD1 += m->SunD1;
		} else {
			afternoon.Rad1h += m->Rad1h;
			afternoon.SunD1 += m->SunD1;
		}
	}

	float noon = forenoon.SunD1 ? (float) afternoon.SunD1 / (float) forenoon.SunD1 : 0;
	xdebug("MOSMIX Rad1h/SunD1 forenoon %d/%d afternoon %d/%d noon=%.1f", forenoon.Rad1h, forenoon.SunD1, afternoon.Rad1h, afternoon.SunD1, noon);
	return noon;
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

mosmix_t* mosmix_current_slot(time_t now_ts) {
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_t *m = &mosmix[i];
		if ((m->ts - 3600) < now_ts && now_ts < m->ts)
			return m;
	}
	return 0;
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
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_t *m = &mosmix[i];
		if (m->ts) {
			localtime_r(&m->ts, &tm);
			mosmix_t *m24 = &mosmix24[tm.tm_hour];
			if (tm.tm_mday == today)
				memcpy(m24, m, sizeof(mosmix_t));
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
	mosmix_t *m = mosmix_current_slot(now_ts);
	if (m != 0) {
		char *timestr = ctime(&m->ts);
		timestr[strcspn(timestr, "\n")] = 0; // remove any NEWLINE
		int exp1h = m->Rad1h * 3; // guessed
		xlog("MOSMIX current slot index=%d date=%d (%s) TTT=%.1f Rad1H=%d SunD1=%d, expected %d Wh", m->idx, m->ts, timestr, m->TTT, m->Rad1h, m->SunD1, exp1h);
	}

	// calculate total daily values
	mosmix_t m0, m1, m2;
	mosmix_24h(now_ts, 0, &m0);
	mosmix_24h(now_ts, 1, &m1);
	mosmix_24h(now_ts, 2, &m2);
	xlog("MOSMIX Rad1h/SunD1 today %d/%d tomorrow %d/%d tomorrow+1 %d/%d", m0.Rad1h, m0.SunD1, m1.Rad1h, m1.SunD1, m2.Rad1h, m2.SunD1);

	// eod - calculate values till end of day
	mosmix_t sod, eod;
	mosmix_sod_eod(now_ts, &sod, &eod);
	xlog("MOSMIX Rad1h/SunD1 sod %d/%d eod %d/%d", sod.Rad1h, sod.SunD1, eod.Rad1h, eod.SunD1);

	// calculate forenoon/afternoon values
	mosmix_t bn, an;
	mosmix_noon(&bn, &an);

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
