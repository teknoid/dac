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

static void store(char **strings, size_t size) {
	int idx = atoi(strings[0]);
	mosmix_t *m = &mosmix[idx];

	m->idx = idx;
	m->ts = atoi(strings[1]);
	m->TTT = atof(strings[2]) - 273.15;
	m->Rad1h = atoi(strings[3]);
	m->SunD1 = atoi(strings[4]);
}

void mosmix_24h(mosmix_t *sum, time_t now_ts, int day) {
	// Calculate today 0:00:00 and build 24h time frame
	struct tm *today = localtime(&now_ts);
	today->tm_hour = today->tm_min = today->tm_sec = 0;
	time_t ts_from = mktime(today) + 60 * 60 * 24 * day;
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
	char buf[128];
	char *strings[5];

	ZERO(mosmix);

	FILE *fp = fopen(filename, "r");
	if (fp == NULL)
		return xerr("FRONIUS no mosmix data available");

	// header
	if (fgets(buf, 128, fp) == NULL)
		return xerr("FRONIUS no mosmix data available");

	int timestamps = 0;
	while (fgets(buf, 128, fp) != NULL) {
		int i = 0;
		char *p = strtok(buf, ",");
		while (p != NULL) {
			p[strcspn(p, "\n")] = 0; // remove any NEWLINE
			strings[i++] = p;
			p = strtok(NULL, ",");
		}
		store(strings, ARRAY_SIZE(strings));
		timestamps++;
	}

	fclose(fp);
	xlog("MOSMIX loaded %s with %d timestamps", filename, timestamps);
	return 0;
}

int mosmix_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	mosmix_load("/ram/CHEMNITZ.csv");

	// find current slot
	time_t now_ts = time(NULL);
	mosmix_t *m = mosmix_current_slot(now_ts);
	struct tm *slot_time = localtime(&(m->ts));
	char *timestr = asctime(slot_time);
	timestr[strcspn(timestr, "\n")] = 0; // remove any NEWLINE
	xlog("MOSMIX current slot is: %d %s (%d) TTT=%2.1f Rad1H=%d SunD1=%d, expected %d Wh", m->idx, timestr, m->ts, m->TTT, m->Rad1h, m->SunD1, m->Rad1h * MOSMIX_FACTOR);

	// cumulated today, tomorrow, tomorrow + 1
	mosmix_t m0, m1, m2;
	mosmix_24h(&m0, now_ts, 0);
	mosmix_24h(&m1, now_ts, 1);
	mosmix_24h(&m2, now_ts, 2);

	int e0 = m0.Rad1h * MOSMIX_FACTOR;
	int e1 = m1.Rad1h * MOSMIX_FACTOR;
	int e2 = m2.Rad1h * MOSMIX_FACTOR;
	xlog("MOSMIX Rad1h/Wh today %d/%d tomorrow %d/%d tomorrow+1 %d/%d", m0.Rad1h, e0, m1.Rad1h, e1, m2.Rad1h, e2);

	return 0;
}

#ifdef MOSMIX_MAIN
int main(int argc, char **argv) {
	return mosmix_main(argc, argv);
}
#endif

