// gcc -Wall -DMOSMIX_MAIN -I ./include/ -o mosmix mosmix.c utils.c -lpthread

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include <sys/sysinfo.h>

#include "mosmix.h"
#include "utils.h"
#include "mcp.h"

// !!! never calculate average from errors/percents
// !!! always sum up values and then calculate
// 33	44				75%
// 333	555				60%
// 3333	6666			50%
// --------------------------
// 3699	7265	50,9%	61,6%

// hexdump -v -e '19 "%6d ""\n"' /var/lib/mcp/solar-mosmix-history.bin
#define MOSMIX_HISTORY			"solar-mosmix-history.bin"

// csvfilter.sh /run/mcp/mosmix-history.csv 12
#define MOSMIX_HISTORY_CSV		"mosmix-history.csv"
#define MOSMIX_FACTORS_CSV		"mosmix-factors.csv"
#define MOSMIX_TODAY_CSV		"mosmix-today.csv"
#define MOSMIX_TOMORROW_CSV		"mosmix-tomorrow.csv"

// temperature coefficient scaled as x100
#define TCOP					-34

#define SUM_EXP(m)				((m)->exp1  + (m)->exp2  + (m)->exp3  + (m)->exp4)
#define SUM_MPPT(m)				((m)->mppt1 + (m)->mppt2 + (m)->mppt3 + (m)->mppt4)

#define HISTORY_SIZE			(24 * 7)

#define NOISE					10
#define BASELOAD				250
#define FMAX					9999
#define FRMAX					9999
#define FSMAX					999
#define MOSMIX_COLUMNS			6

// all raw values from kml file
static mosmix_csv_t mosmix_csv[256];

// 24h slots over one week and access pointers
// today and tommorow contains only forecast data
// history contains all data (forecast, mppt, factors and errors)
static mosmix_t today[24], tomorrow[24], history[HISTORY_SIZE];
#define TODAY(h)				(&today[h])
#define TOMORROW(h)				(&tomorrow[h])
#define HISTORY(d, h)			(&history[24 * d + h])

// rad1/sund1 coefficients per MPPT and hour and access pointer
static factor_t factors[24];
#define FACTORS(h)				(&factors[h])

// fake dummy average akkus over 24/7
static int fake_loads[24] = { 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 173 };

static void sum(mosmix_t *to, mosmix_t *from) {
	int *t = (int*) to;
	int *f = (int*) from;
	for (int i = 0; i < MOSMIX_SIZE; i++) {
		*t = *t + *f;
		t++;
		f++;
	}
}
static void parse(char **strings, size_t size) {
	int idx = atoi(strings[0]);
	mosmix_csv_t *m = &mosmix_csv[idx];

	m->idx = idx;
	m->ts = atoi(strings[1]);
	m->TTT = atof(strings[2]) - 273.15; // convert to °C
	m->Rad1h = atoi(strings[3]);
	m->SunD1 = atoi(strings[4]) * 100 / 3600; // convert to 0..100 percent
	m->RSunD = atoi(strings[5]);
}

// calculate expected pv as combination of raw mosmix values with mppt specific coefficients
static void expecteds(mosmix_t *m, factor_t *f) {
	float fSunD1 = 1.0 + (float) m->SunD1 / 1000;
	float ftco = 1.0 + (float) (m->TTT - 25) * (float) TCOP / 100 / 100;

	// xdebug("TTT=%d fSunD1=%.3f ftco=%.3f", m->TTT, fSunD1, ftco);

	float f1 = (float) m->Rad1h * (float) f->r1 * fSunD1 * ftco;
	m->exp1 = round100(f1 / 100);
	float f2 = (float) m->Rad1h * (float) f->r2 * fSunD1 * ftco;
	m->exp2 = round100(f2 / 100);
	float f3 = (float) m->Rad1h * (float) f->r3 * fSunD1 * ftco;
	m->exp3 = round100(f3 / 100);
	float f4 = (float) m->Rad1h * (float) f->r4 * fSunD1 * ftco;
	m->exp4 = round100(f4 / 100);
}

static void errors(mosmix_t *m) {
	// calculate errors as actual - expected
	m->diff1 = m->mppt1 ? m->mppt1 - m->exp1 : 0;
	m->diff2 = m->mppt2 ? m->mppt2 - m->exp2 : 0;
	m->diff3 = m->mppt3 ? m->mppt3 - m->exp3 : 0;
	m->diff4 = m->mppt4 ? m->mppt4 - m->exp4 : 0;

	// calculate errors as actual / expected
	m->err1 = m->mppt1 && m->exp1 ? m->mppt1 * 100 / m->exp1 : 100;
	m->err2 = m->mppt2 && m->exp2 ? m->mppt2 * 100 / m->exp2 : 100;
	m->err3 = m->mppt3 && m->exp3 ? m->mppt3 * 100 / m->exp3 : 100;
	m->err4 = m->mppt4 && m->exp4 ? m->mppt4 * 100 / m->exp4 : 100;
}

static void collect(struct tm *now, mosmix_t *mtomorrow, mosmix_t *mtoday, mosmix_t *msod, mosmix_t *meod) {
	for (int h = 0; h < 24; h++) {
		factor_t *f = FACTORS(h);
		mosmix_t *m0 = TODAY(h);
		mosmix_t *m1 = TOMORROW(h);

		// collect today
		expecteds(m0, f);
		mtoday->exp1 += m0->exp1;
		mtoday->exp2 += m0->exp2;
		mtoday->exp3 += m0->exp3;
		mtoday->exp4 += m0->exp4;

		// collect tomorrow
		expecteds(m1, f);
		mtomorrow->exp1 += m1->exp1;
		mtomorrow->exp2 += m1->exp2;
		mtomorrow->exp3 += m1->exp3;
		mtomorrow->exp4 += m1->exp4;

		// calculate sod/eod
		if (h < now->tm_hour + 1) {
			// full elapsed hours into sod
			msod->exp1 += m0->exp1;
			msod->exp2 += m0->exp2;
			msod->exp3 += m0->exp3;
			msod->exp4 += m0->exp4;
		} else if (h > now->tm_hour + 1) {
			// full remaining hours into eod
			meod->exp1 += m0->exp1;
			meod->exp2 += m0->exp2;
			meod->exp3 += m0->exp3;
			meod->exp4 += m0->exp4;
		} else {
			// current hour - split at current minute
			int xs1 = m0->exp1 * now->tm_min / 60, xe1 = m0->exp1 - xs1;
			int xs2 = m0->exp2 * now->tm_min / 60, xe2 = m0->exp2 - xs2;
			int xs3 = m0->exp3 * now->tm_min / 60, xe3 = m0->exp3 - xs3;
			int xs4 = m0->exp4 * now->tm_min / 60, xe4 = m0->exp4 - xs4;
			// elapsed minutes into sod
			msod->exp1 += xs1;
			msod->exp2 += xs2;
			msod->exp3 += xs3;
			msod->exp4 += xs4;
			// remaining minutes into eod
			meod->exp1 += xe1;
			meod->exp2 += xe2;
			meod->exp3 += xe3;
			meod->exp4 += xe4;
		}
	}
}

// recalc expected and errors
static void recalc_expected() {
	for (int h = 0; h < 24; h++) {
		factor_t *f = FACTORS(h);

		// today
		mosmix_t *m0 = TODAY(h);
		expecteds(m0, f);
		errors(m0);

		// tomorrow
		mosmix_t *m1 = TOMORROW(h);
		expecteds(m1, f);
		errors(m1);

		// history
		for (int d = 0; d < 7; d++) {
			mosmix_t *m = HISTORY(d, h);
			expecteds(m, f);
			errors(m);
		}
	}
}

// update today and tomorrow with actual data from mosmix kml download
static void update_today_tomorrow(struct tm *now) {
	struct tm tm;
	int day_today = now->tm_yday;
	int day_tomorrow = now->tm_yday < 364 ? now->tm_yday + 1 : 0;

	// loop over one week
	for (int i = 0; i < HISTORY_SIZE; i++) {
		mosmix_csv_t *mcsv = &mosmix_csv[i];

//		struct tm utcTime, localTime;
//		gmtime_r(&mcsv->ts, &utcTime);
//		localtime_r(&mcsv->ts, &localTime);
//		xlog("MOSMIX ts=%d   utcTime=%s", mcsv->ts, asctime(&utcTime));
//		xlog("MOSMIX ts=%d localTime=%s", mcsv->ts, asctime(&localTime));

		// find slot to update
		localtime_r(&mcsv->ts, &tm);
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
		m->TTT = mcsv->TTT;
		if (m->Rad1h)
			xdebug("MOSMIX updated %02d.%02d. hour %02d Rad1h=%d SunD1=%d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, m->Rad1h, m->SunD1);
		else
			// SunD1 without Rad1h is not possible
			m->SunD1 = 0;
	}
}

// Rad1h factors determination by averaging mosmix history actual vs. expected per mppt
static void calculate_factors() {
	ZERO(factors);

	for (int h = 0; h < 24; h++) {
		factor_t fd[7];
		memset(fd, 0, sizeof(fd));

		for (int d = 0; d < 7; d++) {
			mosmix_t *m = HISTORY(d, h);
			fd[d].r1 = m->Rad1h && m->mppt1 ? m->mppt1 * 100 / m->Rad1h : 0;
			fd[d].r2 = m->Rad1h && m->mppt2 ? m->mppt2 * 100 / m->Rad1h : 0;
			fd[d].r3 = m->Rad1h && m->mppt3 ? m->mppt3 * 100 / m->Rad1h : 0;
			fd[d].r4 = m->Rad1h && m->mppt4 ? m->mppt4 * 100 / m->Rad1h : 0;
		}

		// clear smallest
		int f1min = fd[0].r1, d1min = 0, f2min = fd[0].r2, d2min = 0, f3min = fd[0].r3, d3min = 0, f4min = fd[0].r4, d4min = 0;
		for (int d = 0; d < 7; d++) {
			if (fd[d].r1 < f1min)
				d1min = d;
			if (fd[d].r2 < f2min)
				d2min = d;
			if (fd[d].r3 < f3min)
				d3min = d;
			if (fd[d].r4 < f4min)
				d4min = d;
		}
		fd[d1min].r1 = fd[d1min].r1 ? 100 : 0;
		fd[d2min].r2 = fd[d1min].r2 ? 100 : 0;
		fd[d3min].r3 = fd[d1min].r3 ? 100 : 0;
		fd[d4min].r4 = fd[d1min].r4 ? 100 : 0;

		// clear biggest
		int f1max = fd[0].r1, d1max = 0, f2max = fd[0].r2, d2max = 0, f3max = fd[0].r3, d3max = 0, f4max = fd[0].r4, d4max = 0;
		for (int d = 0; d < 7; d++) {
			if (fd[d].r1 > f1max)
				d1max = d;
			if (fd[d].r2 > f2max)
				d2max = d;
			if (fd[d].r3 > f3max)
				d3max = d;
			if (fd[d].r4 > f4max)
				d4max = d;
		}
		fd[d1max].r1 = fd[d1max].r1 ? 100 : 0;
		fd[d2max].r2 = fd[d2max].r2 ? 100 : 0;
		fd[d3max].r3 = fd[d3max].r3 ? 100 : 0;
		fd[d4max].r4 = fd[d4max].r4 ? 100 : 0;

		// calculate factors
		factor_t *f = FACTORS(h);
		memset(f, 0, sizeof(factor_t));
		for (int d = 0; d < 7; d++) {
			f->r1 += fd[d].r1;
			f->r2 += fd[d].r2;
			f->r3 += fd[d].r3;
			f->r4 += fd[d].r4;
		}
		f->r1 /= 7;
		f->r2 /= 7;
		f->r3 /= 7;
		f->r4 /= 7;

		if (f->r1)
			xlog("MOSMIX MPPT1 h=%2d 1=%3d 2=%3d 3=%3d 4=%3d 5=%3d 6=%3d 7=%3d fr=%3d", h, fd[0].r1, fd[1].r1, fd[2].r1, fd[3].r1, fd[4].r1, fd[5].r1, fd[6].r1, f->r1);
	}

	mosmix_t mc;
	store_table_csv(factors, FACTOR_SIZE, 24, FACTOR_HEADER, RUN SLASH MOSMIX_FACTORS_CSV);
	dump_table(factors, FACTOR_SIZE, 24, 0, "MOSMIX factors", FACTOR_HEADER);
	icumulate(&mc, factors, FACTOR_SIZE, 24);
	dump_array(&mc, FACTOR_SIZE, "[++]", 0);
}

void mosmix_mppt(struct tm *now, int mppt1, int mppt2, int mppt3, int mppt4) {
	mosmix_t *m = TODAY(now->tm_hour);

	// update actual pv
	m->mppt1 = mppt1 > NOISE ? mppt1 : 0;
	m->mppt2 = mppt2 > NOISE ? mppt2 : 0;
	m->mppt3 = mppt3 > NOISE ? mppt3 : 0;
	m->mppt4 = mppt4 > NOISE ? mppt4 : 0;

	// recalc errors
	errors(m);
	xdebug("MOSMIX forecast mppt Wh %5d %5d %5d %5d sum %d", m->mppt1, m->mppt2, m->mppt3, m->mppt4, m->mppt1 + m->mppt2 + m->mppt3 + m->mppt4);
	xdebug("MOSMIX forecast exp  Wh %5d %5d %5d %5d sum %d", m->exp1, m->exp2, m->exp3, m->exp4, m->exp1 + m->exp2 + m->exp3 + m->exp4);
	xdebug("MOSMIX forecast err  Wh %5d %5d %5d %5d sum %d", m->diff1, m->diff2, m->diff3, m->diff4, m->diff1 + m->diff2 + m->diff3 + m->diff4);
	xdebug("MOSMIX forecast err  %%  %5.2f %5.2f %5.2f %5.2f", FLOAT100(m->err1), FLOAT100(m->err2), FLOAT100(m->err3), FLOAT100(m->err4));

	// save to history
	mosmix_t *mh = HISTORY(now->tm_wday, now->tm_hour);
	memcpy(mh, m, sizeof(mosmix_t));
}

void mosmix_scale(struct tm *now, int *succ1, int *succ2) {
	mosmix_t mtoday, mtomorrow, msod, meod;
	*succ1 = *succ2 = 0;

	// before scale
	collect(now, &mtomorrow, &mtoday, &msod, &meod);
	int exp1 = SUM_EXP(&mtoday);
	int sodx1 = SUM_EXP(&msod);
	int sodm1 = SUM_MPPT(&msod);
	*succ1 = sodx1 ? sodm1 * 1000 / sodx1 : 0;

	// nothing to scale
	if (TODAY(now->tm_hour)->Rad1h == 0)
		return;

	int ch = now->tm_hour + 1;
	mosmix_t m;
	ZERO(m);

	// sum up mppt and expected till now
	for (int h = 0; h < ch; h++)
		sum(&m, TODAY(h));

	// calculate diff and error
	errors(&m);

	// high limit to 200%
	HICUT(m.err1, 200);
	HICUT(m.err2, 200);
	HICUT(m.err3, 200);
	HICUT(m.err4, 200);

	// thresholds for scaling
	int s1 = (m.diff1 < -100 || m.diff1 > 100) && (m.err1 < 90 || m.err1 > 120);
	int s2 = (m.diff2 < -100 || m.diff2 > 100) && (m.err2 < 90 || m.err2 > 120);
	int s3 = (m.diff3 < -100 || m.diff3 > 100) && (m.err3 < 90 || m.err3 > 120);
	int s4 = (m.diff4 < -100 || m.diff4 > 100) && (m.err4 < 90 || m.err4 > 120);

	if (s1)
		xlog("MOSMIX scaling MPPT1 by %5.2f (%s%d)", FLOAT100(m.err1), m.diff1 > 0 ? "+" : "", m.diff1);
	if (s2)
		xlog("MOSMIX scaling MPPT2 by %5.2f (%s%d)", FLOAT100(m.err2), m.diff2 > 0 ? "+" : "", m.diff2);
	if (s3)
		xlog("MOSMIX scaling MPPT3 by %5.2f (%s%d)", FLOAT100(m.err3), m.diff3 > 0 ? "+" : "", m.diff3);
	if (s4)
		xlog("MOSMIX scaling MPPT4 by %5.2f (%s%d)", FLOAT100(m.err4), m.diff4 > 0 ? "+" : "", m.diff4);

	// nothing to scale
	if (!s1 && !s2 && !s3 && !s4)
		return;

	// scale all sod and eod - this corrects the success factor to ~100%
	for (int h = 0; h < 24; h++) {
		if (s1)
			TODAY(h)->exp1 = TODAY(h)->exp1 * m.err1 / 100;
		if (s2)
			TODAY(h)->exp2 = TODAY(h)->exp2 * m.err2 / 100;
		if (s3)
			TODAY(h)->exp3 = TODAY(h)->exp3 * m.err3 / 100;
		if (s4)
			TODAY(h)->exp4 = TODAY(h)->exp4 * m.err4 / 100;
	}

	// after scale
	collect(now, &mtomorrow, &mtoday, &msod, &meod);
	int exp2 = SUM_EXP(&mtoday);
	int sodx2 = SUM_EXP(&msod);
	int sodm2 = SUM_MPPT(&msod);
	*succ2 = sodx2 ? sodm2 * 1000 / sodx2 : 0;

	float fs1 = FLOAT10(*succ1), fs2 = FLOAT10(*succ2);
	xlog("MOSMIX scaling   before: total=%d mppt=%d exp=%d succ=%.1f%%   after: total=%d mppt=%d exp=%d succ=%.1f%%", exp1, sodm1, sodx1, fs1, exp2, sodm2, sodx2, fs2);
}

// collect total expected today, tomorrow and till end of day / start of day
void mosmix_collect(struct tm *now, int *itomorrow, int *itoday, int *isod, int *ieod) {
	mosmix_t mtoday, mtomorrow, msod, meod;
	memset(&mtoday, 0, sizeof(mosmix_t));
	memset(&mtomorrow, 0, sizeof(mosmix_t));
	memset(&msod, 0, sizeof(mosmix_t));
	memset(&meod, 0, sizeof(mosmix_t));

	collect(now, &mtomorrow, &mtoday, &msod, &meod);
	*itomorrow = SUM_EXP(&mtomorrow);
	*itoday = SUM_EXP(&mtoday);
	*isod = SUM_EXP(&msod);
	*ieod = SUM_EXP(&meod);
	xlog("MOSMIX collect today=%d tomorrow=%d sod=%d eod=%d", *itoday, *itomorrow, *isod, *ieod);

	// validate
	if (*itoday != *isod + *ieod)
		xdebug("MOSMIX sod/eod calculation error %d != %d + %d", *itoday, *isod, *ieod);
}

// night: collect akku power when pv is not enough
void mosmix_needed(struct tm *now, int baseload, int *needed, int *minutes, int akkus[], int loads[]) {
	char line[LINEBUF * 2], value[48];
	int ch = now->tm_hour < 23 ? now->tm_hour + 1 : 0, h = ch, night = 0, midnight = 0;
	*needed = *minutes = 0;

	strcpy(line, "MOSMIX survive h:a:x");
	while (1) {
		mosmix_t *m = midnight ? TOMORROW(h) : TODAY(h);

		// current hour -> partly, remaining hours -> full
		int x = h == ch ? SUM_EXP(m) * (60 - now->tm_min) / 60 : SUM_EXP(m);
		int a = h == ch ? akkus[h] * (60 - now->tm_min) / 60 : akkus[h];
		int l = h == ch ? loads[h] * (60 - now->tm_min) / 60 : loads[h];

		// akku might be limited on discharge - use bigger one
		int al = l > a ? l : a;

		// akku is discharging and expected below baseload - night
		if (a > 0 && x < baseload) {
			snprintf(value, 48, " %d:%d:%d", h, al, x);
			strcat(line, value);
			*needed += al;
			*minutes += h == ch ? 60 - now->tm_min : 60;
			night = 1;
		}

		// reached end of night or high noon this/next day
		if ((night && x > baseload) || (night && h == 12) || (midnight && h == 12))
			break;

		// reached midnight
		if (++h == 24) {
			midnight = 1;
			h = 0;
		}
	}

	*needed = round10(*needed);
	snprintf(value, 48, " --> need=%d hours=%.1f", *needed, FLOAT60(*minutes));
	strcat(line, value);
	xdebug(line);
}

// day: collect heating power where we can use pv for
int mosmix_heating(struct tm *now, int power) {
	char line[LINEBUF], value[48];
	int ch = now->tm_hour < 23 ? now->tm_hour + 1 : 0, hours = 0, needed = 0;

	strcpy(line, "MOSMIX heating h:x:p");
	for (int h = ch; h < 24; h++) {
		mosmix_t *m = TODAY(h);
		// current hour -> partly, remaining hours -> full
		int p = h == ch ? power * (60 - now->tm_min) / 60 : power;
		int x = h == ch ? SUM_EXP(m) * (60 - now->tm_min) / 60 : SUM_EXP(m);
		if (x > p) {
			snprintf(value, 48, " %d:%d:%d", h, x, p);
			strcat(line, value);
			needed += p;
			hours++;
		}
	}

	snprintf(value, 48, " --> %d hours = %d", hours, needed);
	strcat(line, value);
	xdebug(line);
	return needed;
}

// sum up 24 mosmix slots for one day (with offset)
void mosmix_24h(int day, mosmix_csv_t *sum) {
	LOCALTIME

	// calculate today 0:00:00 as start and +24h as end time frame
	now->tm_hour = now->tm_min = now->tm_sec = 0;
	time_t ts_from = mktime(now) + 60 * 60 * 24 * day;
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
	icumulate(&m, today, MOSMIX_SIZE, 24);
	dump_table(today, MOSMIX_SIZE, 24, now->tm_hour, "MOSMIX today", MOSMIX_HEADER);
	dump_array(&m, MOSMIX_SIZE, "[++]", 0);
}

void mosmix_dump_tomorrow(struct tm *now) {
	mosmix_t m;
	icumulate(&m, tomorrow, MOSMIX_SIZE, 24);
	dump_table(tomorrow, MOSMIX_SIZE, 24, now->tm_hour, "MOSMIX tomorrow", MOSMIX_HEADER);
	dump_array(&m, MOSMIX_SIZE, "[++]", 0);
}

void mosmix_dump_history(struct tm *now) {
	dump_table(history, MOSMIX_SIZE, HISTORY_SIZE, now->tm_wday * 24 + now->tm_hour, "MOSMIX history full", MOSMIX_HEADER);
}

void mosmix_dump_history_hours(int h) {
	xlog("MOSMIX history hours\n    "MOSMIX_HEADER);
	char idx[6];
	snprintf(idx, 6, "[%02d]", h);
	for (int d = 0; d < 7; d++)
		dump_array(HISTORY(d, h), MOSMIX_SIZE, idx, 0);
}

void mosmix_load_state(struct tm *now) {
	load_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));

	ZERO(today);
	ZERO(tomorrow);

	// initially fill today and tomorrow forecasts from history
	for (int h = 0; h < 24; h++) {
		mosmix_t *m1 = TOMORROW(h), *h1 = HISTORY(now->tm_wday < 6 ? now->tm_wday + 1 : 0, h);
		m1->Rad1h = h1->Rad1h;
		m1->SunD1 = h1->SunD1;
		m1->TTT = h1->TTT;

		mosmix_t *m0 = TODAY(h), *h0 = HISTORY(now->tm_wday, h);
		m0->Rad1h = h0->Rad1h;
		m0->SunD1 = h0->SunD1;
		m0->TTT = h0->TTT;

		// today elapsed hours: take over mpptX too
		if (h <= now->tm_hour) {
			m0->mppt1 = h0->mppt1;
			m0->mppt2 = h0->mppt2;
			m0->mppt3 = h0->mppt3;
			m0->mppt4 = h0->mppt4;
		}
	}
}

void mosmix_store_state() {
	store_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));
}
void mosmix_store_csv() {
	store_table_csv(factors, FACTOR_SIZE, 24, FACTOR_HEADER, RUN SLASH MOSMIX_FACTORS_CSV);
	store_table_csv(history, MOSMIX_SIZE, HISTORY_SIZE, MOSMIX_HEADER, RUN SLASH MOSMIX_HISTORY_CSV);
	store_table_csv(today, MOSMIX_SIZE, 24, MOSMIX_HEADER, RUN SLASH MOSMIX_TODAY_CSV);
	store_table_csv(tomorrow, MOSMIX_SIZE, 24, MOSMIX_HEADER, RUN SLASH MOSMIX_TOMORROW_CSV);
}

int mosmix_load(struct tm *now, const char *filename, int clear) {
	char *strings[MOSMIX_COLUMNS];
	char buf[LINEBUF];

	ZERO(mosmix_csv);

	FILE *fp = fopen(filename, "rt");
	if (fp == NULL)
		return xerr("MOSMIX Cannot open file %s for reading", filename);

	// header
	if (fgets(buf, LINEBUF, fp) == NULL)
		return xerr("MOSMIX no data available");

	int lines = 0;
	while (fgets(buf, LINEBUF, fp) != NULL) {
		int tokens = 0;
		char *p = strtok(buf, ",");
		while (p != NULL) {
			strings[tokens] = p;
			p = strtok(NULL, ",");
			tokens++;
		}
		parse(strings, ARRAY_SIZE(strings));
		lines++;
	}

	fclose(fp);
	xlog("MOSMIX loaded %s containing %d lines", filename, lines);

	if (clear) {
		ZERO(today);
		ZERO(tomorrow);
	}
	update_today_tomorrow(now);
	calculate_factors();
	recalc_expected();
	return 0;
}

static int test() {
	LOCALTIME

	int x = 3333;
	int f = 222;
	printf("io  %d\n", x * f / 100);
	printf("io  %d\n", (x * f) / 100);
	printf("nio %d\n", x * (f / 100));
	printf("nio %d\n", (f / 100) * x);

	float ftc = -.35, fpr = 400.0, ft = 60.0;
	float floss = ftc * (ft - 25.0);
	float fout1 = fpr + (fpr * floss / 100.0);
	float fout2 = fpr * (1 + ftc * (ft - 25.0) / 100.0);
	float fout3 = fpr * (1 + ftc * (ft - 25) / 100);
	printf("ftc=%.2f fpr=%.2f ft=%.2f --> loss=%.2f out1=%.2f out2=%.2f out3=%.2f\n", ftc, fpr, ft, floss, fout1, fout2, fout3);

	int itc = -35, ipr = 400, it = 60;
	int iloss = itc * (it - 25.0) / 100;
	int iout = ipr * (100 + itc * (ft - 25) / 100) / 100;
	printf("itc=%d ipr=%d it=%d --> iloss=%d iout=%d\n", itc, ipr, it, iloss, iout);

	// load state and update forecasts
	mosmix_load_state(now);
	mosmix_load(now, WORK SLASH MARIENBERG, 1);

	// calculate total daily values
	mosmix_csv_t m0, m1, m2;
	mosmix_24h(0, &m0);
	mosmix_24h(1, &m1);
	mosmix_24h(2, &m2);
	xlog("MOSMIX Rad1h/SunD1/RSunD today %d/%d/%d tomorrow %d/%d/%d tomorrow+1 %d/%d/%d", m0.Rad1h, m0.SunD1, m0.RSunD, m1.Rad1h, m1.SunD1, m1.RSunD, m2.Rad1h, m2.SunD1, m2.RSunD);

	int itoday, itomorrow, sod, eod, succ1, succ2;

	// calculate expected today and tomorrow
	xlog("MOSMIX *** now (%02d) ***", now->tm_hour);
	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod);
	mosmix_dump_today(now);
	mosmix_dump_tomorrow(now);

	xlog("MOSMIX *** updated now (%02d) ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_scale(now, &succ1, &succ2);
	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod);

	now->tm_hour = 9;
	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_scale(now, &succ1, &succ2);
	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod);

	now->tm_hour = 12;
	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_scale(now, &succ1, &succ2);
	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod);

	now->tm_hour = 15;
	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
	mosmix_mppt(now, 4000, 3000, 2000, 1000);
	mosmix_scale(now, &succ1, &succ2);
	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod);

	mosmix_dump_history(now);
	mosmix_dump_history_hours(9);
	mosmix_dump_history_hours(12);
	mosmix_dump_history_hours(15);

	now->tm_hour = 16;
	mosmix_dump_today(now);
	mosmix_dump_tomorrow(now);
	int needed, hours;
	mosmix_needed(now, BASELOAD, &needed, &hours, fake_loads, fake_loads);
	mosmix_heating(now, 1500);
	return 0;
}

static int recalc() {
	LOCALTIME

	mosmix_load_state(now);
	mosmix_load(now, WORK SLASH MARIENBERG, 0);
	calculate_factors();
	recalc_expected();
	mosmix_dump_history_hours(12);
	mosmix_store_csv();
	return 0;
}

static int migrate() {
	ZERO(history);

	mosmix_old_t old[HISTORY_SIZE];
	load_blob(STATE SLASH MOSMIX_HISTORY, old, sizeof(old));

	for (int i = 0; i < HISTORY_SIZE; i++) {
		mosmix_old_t *o = &old[i];
		mosmix_t *n = &history[i];
		n->Rad1h = o->Rad1h;
		n->SunD1 = o->SunD1;
		n->TTT = o->TTT;
		n->mppt1 = o->mppt1;
		n->mppt2 = o->mppt2;
		n->mppt3 = o->mppt3;
		n->mppt4 = o->mppt4;
	}

	// test and verify
	store_blob(TMP SLASH MOSMIX_HISTORY, history, sizeof(history));
	// live
	// store_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));
	return 0;
}

static int fix() {
	load_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));

//	mosmix_t *m;
//	m = &history[152];
//	m->mppt3 = m->diff3 = 0;
//	m->err3 = 100;
//	m = &history[139];
//	m->mppt1 = m->mppt2 = m->mppt3 = m->mppt4 = 0;
//	m->diff1 = m->diff2 = m->diff3 = m->diff4 = 0;
//	m->err1 = m->err2 = m->err3 = m->err4 = 100;
//	m = &history[140];
//	m->mppt1 = m->mppt2 = m->mppt3 = m->mppt4 = 0;
//	m->diff1 = m->diff2 = m->diff3 = m->diff4 = 0;
//	m->err1 = m->err2 = m->err3 = m->err4 = 100;

	store_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));
	return 0;
}

static void wget(struct tm *now, const char *id) {
	char ftstamp[32], fname[64], fforecasts[64], ftimestamps[64], furl[256], cmd[288];
	chdir(TMP);

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

static int diffs(struct tm *now) {
	// take over mppt's from history into today
	for (int h = 0; h < 24; h++) {
		mosmix_t *mh = HISTORY(now->tm_wday, h);
		mosmix_t *mt = TODAY(h);
		mt->mppt1 = mh->mppt1;
		mt->mppt2 = mh->mppt2;
		mt->mppt3 = mh->mppt3;
		mt->mppt4 = mh->mppt4;
	}

	int diff_sum = 0;
	for (int h = 0; h < 24; h++) {
		mosmix_t *m = TODAY(h);
		factor_t *f = FACTORS(h);
		expecteds(m, f);
		int mppt = SUM_MPPT(m);
		int expt = SUM_EXP(m);
		int diff = mppt - expt;
		diff_sum += abs(diff);
		if (diff)
			printf("hour %02d mppt %4d expt %4d   --> err %4d\n", h, mppt, expt, diff);
	}
	return diff_sum;
}

static int compare() {
	load_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));

	// yesterday
	time_t ts_yday = time(NULL);
	ts_yday -= 60 * 60 * 24;
	struct tm tm_yday, *yday = &tm_yday;
	localtime_r(&ts_yday, &tm_yday);

	wget(yday, "10579");
	mosmix_load(yday, TMP SLASH MARIENBERG, 1);
	int dm = diffs(yday);

	wget(yday, "10577");
	mosmix_load(yday, TMP SLASH CHEMNITZ, 1);
	int dc = diffs(yday);

	wget(yday, "N4464");
	mosmix_load(yday, TMP SLASH BRAUNSDORF, 1);
	int db = diffs(yday);

	printf("%4d-%02d-%02d Marienberg=%d Chemnitz=%d Braunsdorf=%d\n", yday->tm_year + 1900, yday->tm_mon + 1, yday->tm_mday, dm, dc, db);
	return 0;
}

int mosmix_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	// no arguments - test
	if (argc == 1)
		return test();

	int c;
	while ((c = getopt(argc, argv, "cfmrt")) != -1) {
		// printf("getopt %c\n", c);
		switch (c) {
		case 'c':
			return compare();
		case 'f':
			return fix();
		case 'm':
			return migrate();
		case 'r':
			return recalc();
		case 't':
			return test();
		default:
			xlog("unknown getopt %c", c);
		}
	}

	return 0;
}

#ifdef MOSMIX_MAIN
int main(int argc, char **argv) {
	return mosmix_main(argc, argv);
}
#endif
