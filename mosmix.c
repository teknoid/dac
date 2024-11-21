#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

static mosmix_t mosmix[256];

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

// calculate today 00:00:00 as start and now as end time frame
void mosmix_sod(mosmix_t *sum, time_t now_ts) {
	struct tm tm;
	localtime_r(&now_ts, &tm);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	time_t ts_from = mktime(&tm) + 1;

	ZEROP(sum);
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_t *m = &mosmix[i];
		if (ts_from < m->ts && m->ts < now_ts) { // exclude current hour
			sum->Rad1h += m->Rad1h;
			sum->SunD1 += m->SunD1;
		}
	}
}

// calculate now+1h as start and today 23:59:59 as end time frame
void mosmix_eod(mosmix_t *sum, time_t now_ts) {
	struct tm tm;
	localtime_r(&now_ts, &tm);
	tm.tm_hour = 23;
	tm.tm_min = 59;
	tm.tm_sec = 59;
	time_t ts_to = mktime(&tm);

	ZEROP(sum);
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_t *m = &mosmix[i];
		if (m->ts > (now_ts + 3600) && m->ts < ts_to) { // exclude current hour
			sum->Rad1h += m->Rad1h;
			sum->SunD1 += m->SunD1;
		}
	}
}

// calculate today 0:00:00 as start and +24h as end time frame
void mosmix_24h(mosmix_t *sum, time_t now_ts, int day) {
	struct tm tm;
	localtime_r(&now_ts, &tm);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	time_t ts_from = mktime(&tm) + 60 * 60 * 24 * day;
	time_t ts_to = ts_from + 60 * 60 * 24; // + 1 day

	ZEROP(sum);
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_t *m = &mosmix[i];
		if (ts_from < m->ts && m->ts < ts_to) {
			sum->Rad1h += m->Rad1h;
			sum->SunD1 += m->SunD1;
		}
	}
}

// calculate today 0:00:00 as start and +24h as end time frame
void mosmix_noon(mosmix_t *forenoon, mosmix_t *afternoon, time_t now_ts) {
	struct tm tm;
	localtime_r(&now_ts, &tm);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	time_t ts_from = mktime(&tm);
	time_t ts_noon = ts_from + 60 * 60 * 12; // high noon
	time_t ts_to = ts_from + 60 * 60 * 24; // + 1 day

	ZEROP(forenoon);
	ZEROP(afternoon);
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_t *m = &mosmix[i];
		if (ts_from < m->ts && m->ts < ts_to) {
			if (m->ts <= ts_noon) {
				forenoon->Rad1h += m->Rad1h;
				forenoon->SunD1 += m->SunD1;
			} else {
				afternoon->Rad1h += m->Rad1h;
				afternoon->SunD1 += m->SunD1;
			}
		}
	}
}

// calculate hours to survive darkness
int mosmix_survive(time_t now_ts, int rad1h_min) {
	struct tm tm;
	localtime_r(&now_ts, &tm);
	tm.tm_hour = 23;
	tm.tm_min = 59;
	tm.tm_sec = 59;
	time_t ts_midnight = mktime(&tm) + 1;

	int midnight;
	for (midnight = 0; midnight < ARRAY_SIZE(mosmix); midnight++)
		if (mosmix[midnight].ts == ts_midnight)
			break;

	int from = midnight;
	while (from--)
		if (mosmix[from].Rad1h > rad1h_min || mosmix[from].ts < now_ts)
			break; // sundown or now

	int to = midnight;
	while (to++ < ARRAY_SIZE(mosmix))
		if (mosmix[to].Rad1h > rad1h_min)
			break; // sunrise

	from += 1;
	to -= 1;
	int hours = to - from + 1;
	xlog("MOSMIX survive=%dh from=%d/%d midnight=%d/%d to=%d/%d min=%d", hours, from, mosmix[from].Rad1h, midnight, mosmix[midnight].Rad1h, to, mosmix[to].Rad1h, rad1h_min);
	return hours;
}

mosmix_t* mosmix_current_slot(time_t now_ts) {
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_t *m = &mosmix[i];
		if ((m->ts - 3600) < now_ts && now_ts < m->ts)
			return m;
	}
	return 0;
}

int mosmix_load(const char *filename) {
	char buf[LINEBUF];
	char *strings[MOSMIX_COLUMNS];

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
	return 0;
}

int mosmix_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	time_t now_ts = time(NULL);
	mosmix_load(CHEMNITZ);

	// find current slot
	mosmix_t *m = mosmix_current_slot(now_ts);
	if (m != 0) {
		char *timestr = ctime(&m->ts);
		timestr[strcspn(timestr, "\n")] = 0; // remove any NEWLINE
		int exp1h = m->Rad1h * 3; // guessed
		xlog("MOSMIX current slot index=%d date=%d (%s) TTT=%.1f Rad1H=%d SunD1=%d, expected %d Wh", m->idx, m->ts, timestr, m->TTT, m->Rad1h, m->SunD1, exp1h);
	}

	// eod - calculate values till end of day
	mosmix_t eod;
	mosmix_eod(&eod, now_ts);
	xlog("MOSMIX EOD Rad1h/SunD1 %d/%d", eod.Rad1h, eod.SunD1);

	// calculate total daily values
	mosmix_t m0, m1, m2;
	mosmix_24h(&m0, now_ts, 0);
	mosmix_24h(&m1, now_ts, 1);
	mosmix_24h(&m2, now_ts, 2);
	xlog("MOSMIX Rad1h/SunD1 today %d/%d tomorrow %d/%d tomorrow+1 %d/%d", m0.Rad1h, m0.SunD1, m1.Rad1h, m1.SunD1, m2.Rad1h, m2.SunD1);

	// calculate forenoon/afternoon values
	mosmix_t bn, an;
	mosmix_noon(&bn, &an, now_ts);
	float noon = bn.SunD1 ? (float) an.SunD1 / (float) bn.SunD1 : 0;
	xdebug("MOSMIX Rad1h/SunD1 forenoon %d/%d afternoon %d/%d noon=%.1f", bn.Rad1h, bn.SunD1, an.Rad1h, an.SunD1, noon);

	mosmix_survive(now_ts, 100);
	return 0;
}

#ifdef MOSMIX_MAIN
int main(int argc, char **argv) {
	return mosmix_main(argc, argv);
}
#endif
