#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

static mosmix_t mosmix[256];

// #define MOSMIX_MAIN

static void parse(char **strings, size_t size) {
	int idx = atoi(strings[0]);
	mosmix_t *m = &mosmix[idx];

	m->idx = idx;
	m->ts = atoi(strings[1]);
	m->TTT = atof(strings[2]) - 273.15;
	m->Rad1h = atoi(strings[3]);
	m->SunD1 = atoi(strings[4]);
}

// calculate now as start and today 23:59:59+1 as end time frame
void mosmix_eod(mosmix_t *sum, time_t now_ts) {
	struct tm today;
	localtime_r(&now_ts, &today);
	today.tm_hour = 23;
	today.tm_min = 59;
	today.tm_sec = 59;
	time_t ts_to = mktime(&today) + 1;

	ZERO(sum);
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_t *m = &mosmix[i];
		if (now_ts < m->ts && m->ts < ts_to) {
			sum->Rad1h += m->Rad1h;
			sum->SunD1 += m->SunD1;
		}
	}
}

// calculate today 0:00:00 as start and +24h as end time frame
void mosmix_24h(mosmix_t *sum, time_t now_ts, int day) {
	struct tm today;
	localtime_r(&now_ts, &today);
	today.tm_hour = today.tm_min = today.tm_sec = 0;
	time_t ts_from = mktime(&today) + 60 * 60 * 24 * day;
	time_t ts_to = ts_from + 60 * 60 * 24; // + 1 day

	ZERO(sum);
	for (int i = 0; i < ARRAY_SIZE(mosmix); i++) {
		mosmix_t *m = &mosmix[i];
		if (ts_from < m->ts && m->ts < ts_to) {
			sum->Rad1h += m->Rad1h;
			sum->SunD1 += m->SunD1;
		}
	}
}

mosmix_t* mosmix_current_slot(time_t now_ts) {
	for (int i = 0; i < ARRAY_SIZE(mosmix) - 1; i++) {
		mosmix_t *m0 = &mosmix[i];
		mosmix_t *m1 = &mosmix[i + 1];
		if (m0->ts < now_ts && now_ts < m1->ts)
			return m1;
	}
	return 0;
}

int mosmix_load(const char *filename) {
	char buf[LINEBUF];
	char *strings[MOSMIX_COLUMNS];

	ZERO(mosmix);

	FILE *fp = fopen(filename, "r");
	if (fp == NULL)
		return xerr("MOSMIX no data available");

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
		int exp1h = m->Rad1h * MOSMIX_DEFAULT;
		xlog("MOSMIX current slot index=%d date=%d %s (%d) TTT=%2.1f Rad1H=%d SunD1=%d, expected %d Wh", m->idx, timestr, m->ts, m->TTT, m->Rad1h, m->SunD1, exp1h);
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

	return 0;
}

#ifdef MOSMIX_MAIN
int main(int argc, char **argv) {
	return mosmix_main(argc, argv);
}
#endif
