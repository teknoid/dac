// gcc -Wall -DMOSMIX_MAIN -I ./include/ -o mosmix mosmix.c utils.c frozen.c

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

#include "mosmix.h"
#include "frozen.h"
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
#define MOSMIX_TOMORROW			"solar-mosmix-tomorrow.bin"
#define MOSMIX_TODAY			"solar-mosmix-today.bin"

// csvfilter.sh /run/mcp/mosmix-history.csv 12
#define MOSMIX_FACTORS_CSV		"mosmix-factors.csv"
#define MOSMIX_HISTORY_CSV		"mosmix-history.csv"
#define MOSMIX_TOMORROW_CSV		"mosmix-tomorrow.csv"
#define MOSMIX_TODAY_CSV		"mosmix-today.csv"

#define JSON					".json"
#define CSV						".csv"

// temperature coefficient scaled as x100
#define TCOP					-34

#define SUM_EXP(m)				((m)->exp1  + (m)->exp2  + (m)->exp3  + (m)->exp4)
#define SUM_MPPT(m)				((m)->mppt1 + (m)->mppt2 + (m)->mppt3 + (m)->mppt4)
#define FTCO(m)					(1.0 + (float) (m->TTT - 25) * (float) TCOP / 100 / 100)
#define FX(m)					(m->Rad1h + m->Rad1h * m->Rad1h / 100 + m->SunD1 * 10)

#define HISTORY_SIZE			(24 * 7)
#define CH						(now->tm_hour < 23 ? now->tm_hour + 1 : 0)

#define NOISE					10
#define BASELOAD				170
#define LINEBUF					256

// all raw values from kml file
static csv_t mosmix_csv[256];

// 24h slots over one week and access pointers
// today and tommorow contains only forecast data
// history contains all data (forecast, mppt, factors and errors)
static mosmix_t today[24], tomorrow[24], history[HISTORY_SIZE];
#define TODAY(h)				(&today[h])
#define TOMORROW(h)				(&tomorrow[h])
#define HISTORY(d, h)			(&history[24 * d + h])

// Rad1h / SunD1 factors per MPPT and hour and access pointer
static factor_t factors[24];
#define FACTOR(h)				(&factors[h])

// power over 24 hours today and tomorrow
static power_t powers[48];
#define POWER(h)				(&powers[h])

// average akku and load over 24/7
static int akku[24] = { 289, 249, 245, 248, 240, 244, 243, 245, 238, -35, -219, -499, -1125, -1098, -651, -644, -383, -95, 160, 301, 309, 322, 311, 302 };
static int load[24] = { 260, 216, 220, 215, 210, 217, 211, 215, 228, 405, 621, 767, 1119, 1523, 1291, 1135, 949, 439, 266, 272, 293, 303, 301, 289 };

static void sum(mosmix_t *to, mosmix_t *from) {
	int *t = (int*) to;
	int *f = (int*) from;
	for (int i = 0; i < MOSMIX_SIZE; i++) {
		*t = *t + *f;
		t++;
		f++;
	}
}

static void sum_abs(mosmix_t *to, mosmix_t *from) {
	int *t = (int*) to;
	int *f = (int*) from;
	for (int i = 0; i < MOSMIX_SIZE; i++) {
		*t = *t + (*f < 0 ? *f * -1 : *f);
		t++;
		f++;
	}
}

static const char* get_flag(power_t *s) {
	switch (s->flag) {
	case Night:
		return "n";
	case Day:
		return "d";
	case DayLow:
		return "l";
	case Dusk:
		return "↓";
	case Dawn:
		return "↑";
	default:
		return "";
	}
}

static void parse(char **strings, size_t size) {
	int idx = atoi(strings[0]);
	csv_t *m = &mosmix_csv[idx];

	m->idx = idx;
	m->ts = atoi(strings[1]);
	m->TTT = atof(strings[2]) - 273.15; // convert to °C
	m->Rad1h = atoi(strings[3]);
	m->SunD1 = atoi(strings[4]) * 100 / 3600; // convert to 0..100 percent
	// m->RSunD = atoi(strings[5]);
}

// calculate expected pv as combination of raw mosmix values with mppt specific factor
static void expect(mosmix_t *m, factor_t *f) {
	float ftco = FTCO(m); // 1.170 (-20°) to 0.966 (+35°)
	int x = FX(m); // magic factor
	// xdebug("Rad1h=%-4d SunD1=%-3d TTT=%d ftco=%.3f x=%d", m->Rad1h, m->SunD1, m->TTT, ftco, x);

	float f1 = (float) f->r1 * ftco * x;
	float f2 = (float) f->r2 * ftco * x;
	float f3 = (float) f->r3 * ftco * x;
	float f4 = (float) f->r4 * ftco * x;

	m->exp1 = f1 / 1000;
	m->exp2 = f2 / 1000;
	m->exp3 = f3 / 1000;
	m->exp4 = f4 / 1000;

	ZSHAPE(m->exp1, NOISE);
	ZSHAPE(m->exp2, NOISE);
	ZSHAPE(m->exp3, NOISE);
	ZSHAPE(m->exp4, NOISE);
}

// calculate factor from actual mppt
static void factor(mosmix_t *m, factor_t *f) {
	float ftco = FTCO(m); // 1.170 (-20°) to 0.966 (+35°)
	int x = FX(m); // magic factor
	// xdebug("Rad1h=%-4d SunD1=%-3d TTT=%d ftco=%.3f x=%d", m->Rad1h, m->SunD1, m->TTT, ftco, x);

	float f1 = m->Rad1h && m->mppt1 ? (float) m->mppt1 / ftco / x : 0.0;
	float f2 = m->Rad1h && m->mppt2 ? (float) m->mppt2 / ftco / x : 0.0;
	float f3 = m->Rad1h && m->mppt3 ? (float) m->mppt3 / ftco / x : 0.0;
	float f4 = m->Rad1h && m->mppt4 ? (float) m->mppt4 / ftco / x : 0.0;

	f->r1 = f1 * 1000;
	f->r2 = f2 * 1000;
	f->r3 = f3 * 1000;
	f->r4 = f4 * 1000;
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

	// limit error to 300 max
	HICUT(m->err1, 300)
	HICUT(m->err2, 300)
	HICUT(m->err3, 300)
	HICUT(m->err4, 300)
}

static void collect(struct tm *now, mosmix_t *mtomorrow, mosmix_t *mtoday, mosmix_t *msod, mosmix_t *meod) {
	ZEROP(mtomorrow);
	ZEROP(mtoday);
	ZEROP(msod);
	ZEROP(meod);

	for (int h = 0; h < 24; h++) {
		mosmix_t *m1 = TOMORROW(h);
		mosmix_t *m0 = TODAY(h);

		sum(mtomorrow, m1);
		sum(mtoday, m0);

		if (h < now->tm_hour + 1)
			// full elapsed hours into sod
			sum(msod, m0);
		else if (h > now->tm_hour + 1) {
			// full remaining hours into eod
			sum(meod, m0);
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

// collect base load day power starting at current hour
static void collect_day(struct tm *now, int *minutes, int *power) {
	char line[LINEBUF * 2], value[48];

	strcpy(line, "MOSMIX   day h:m:p");
	for (int i = now->tm_hour + 1; i < 24; i++) {
		power_t *p = POWER(i);

		if (!p->day)
			continue;

		// current hour -> partly, remaining hours -> full
		int mins = p->hour == CH ? p->day_mins - now->tm_min : p->day_mins;
		int need = p->hour == CH ? p->day * mins / p->day_mins : p->day;
		if (mins < 0)
			mins = need = 0;

		// dawn - start day count down at end of hour
		if (p->flag == Dawn && p->hour == CH) {
			if (60 - now->tm_min > p->day_mins) {
				mins = p->day_mins;
				need = p->day;
			} else {
				mins = 60 - now->tm_min;
				need = p->day * mins / p->day_mins;
			}
		}

		snprintf(value, 48, " %s%d:%d:%d", get_flag(p), p->hour, mins, need);
		strcat(line, value);
		*minutes += mins;
		*power += need;
	}

	*power = round10(*power);
	snprintf(value, 48, " --> minutes=%d hours=%.1f power=%d", *minutes, FLOAT60(*minutes), *power);
	strcat(line, value);
	if (*minutes)
		xlog(line);
}

// collect base load night power starting at current hour
static void collect_night(struct tm *now, int *minutes, int *power) {
	char line[LINEBUF * 2], value[48];

	strcpy(line, "MOSMIX night h:m:p");
	int i = now->tm_hour + 1, dark = 0;
	while (1) {

		// reached high noon this day
		if (i == 12 && dark)
			break;

		// reached high noon next day
		if (i == 36)
			break;

		power_t *p = POWER(i++);
		if (!p->night_mins || !p->night)
			continue;

		// current hour -> partly, remaining hours -> full
		int mins = p->hour == CH ? p->night_mins - now->tm_min : p->night_mins;
		int need = p->hour == CH ? p->night * mins / p->night_mins : p->night;
		if (mins < 0)
			mins = need = 0;

		// dusk - start night count down at end of hour
		if (p->flag == Dusk && p->hour == CH) {
			if (60 - now->tm_min > p->night_mins) {
				mins = p->night_mins;
				need = p->night;
			} else {
				mins = 60 - now->tm_min;
				need = p->night * mins / p->night_mins;
			}
		}

		snprintf(value, 48, " %s%d:%d:%d", get_flag(p), p->hour, mins, need);
		strcat(line, value);
		*minutes += mins;
		*power += need;
		dark++;
	}

	*power = round10(*power);
	snprintf(value, 48, " --> minutes=%d hours=%.1f power=%d", *minutes, FLOAT60(*minutes), *power);
	strcat(line, value);
	if (*minutes)
		xlog(line);
}

// collect heating power for this day starting at current hour
static void collect_heating(struct tm *now, int *minutes, int *power) {
	char line[LINEBUF * 2], value[48];

	strcpy(line, "MOSMIX  heat h:m:p");
	for (int i = now->tm_hour + 1; i < 24; i++) {
		power_t *p = POWER(i);

		if (!p->heat)
			continue;

		// current hour -> partly, remaining hours -> full
		int mins = p->hour == CH ? p->heat_mins - now->tm_min : p->heat_mins;
		int need = p->hour == CH ? p->heat * mins / p->heat_mins : p->heat;
		if (mins < 0)
			mins = need = 0;

		snprintf(value, 48, " %s%d:%d:%d", get_flag(p), p->hour, mins, need);
		strcat(line, value);
		*minutes += mins;
		*power += need;
	}

	*power = round10(*power);
	snprintf(value, 48, " --> minutes=%d hours=%.1f power=%d", *minutes, FLOAT60(*minutes), *power);
	strcat(line, value);
	if (*minutes)
		xlog(line);
}

// calculate minutes and power for each hour
static void calculate_power(int baseload, int heating) {
	ZERO(powers);

	// fill hour, akku, load and expected for today and tomorrow
	for (int i = 0; i < 48; i++) {
		int h = i % 24;
		mosmix_t *m = i < 24 ? TODAY(h) : TOMORROW(h);
		power_t *p = POWER(i);

		p->hour = h;
		p->akku = akku[h];
		p->load = load[h];
		p->expt = SUM_EXP(m);
	}

	int bday = baseload + baseload / 10 + 1; // +10 over day
	for (int i = 0; i < 48; i++) {
		power_t *p = POWER(i), *p1m = POWER(i > 1 ? i - 1 : 0), *p1p = POWER(i < 47 ? i + 1 : 47);

		// dusk (x -> zero)
		if (p->expt && !p1p->expt) {
			p->night_mins = bday * 60 / p->expt;
			HICUT(p->night_mins, 60)
			p->night = p->night_mins < 60 ? (bday * p->night_mins / 120) : (bday - (p->expt / 2));
			p->day_mins = p->expt * 60 / bday;
			HICUT(p->day_mins, 60)
			p->day = p->day_mins < 60 ? (p->expt * p->day_mins / 120) : bday;
			p->flag = Dusk;
		}

		// dawn (zero -> x)
		if (!p1m->expt && p->expt) {
			p->night_mins = bday * 60 / p->expt;
			HICUT(p->night_mins, 60)
			p->night = p->night_mins < 60 ? (bday * p->night_mins / 120) : (bday - (p->expt / 2));
			p->day_mins = p->expt * 60 / bday;
			HICUT(p->day_mins, 60)
			p->day = p->day_mins < 60 ? (p->expt * p->day_mins / 120) : bday;
			p->flag = Dawn;
		}

		// night (zero) - use akku or load to calculate needed power
		if (!p->flag && !p->expt) {
			p->night_mins = 60;
			p->night = p->load > p->akku ? p->load : p->akku; // akku might be limited on discharge - use bigger one
			p->flag = Night;
		}

		// day (x) but not enough - calculate needed power from baseload difference
		if (!p->flag && p->expt < bday) {
			p->night_mins = 60;
			p->night = bday - p->expt;
			p->day_mins = 60;
			p->day = p->expt;
			p->flag = DayLow;
		}

		// day
		if (!p->flag && p->expt >= bday) {
			p->day_mins = 60;
			p->day = bday;
			p->flag = Day;
		}

		// day and enough to heat
		if (p->expt > heating) {
			p->heat_mins = 60;
			p->heat = heating;
		}
	}
}

// recalc expected and errors
static void recalc_expected() {
	for (int h = 0; h < 24; h++) {
		factor_t *f = FACTOR(h);

		// today
		mosmix_t *m0 = TODAY(h);
		expect(m0, f);
		errors(m0);

		// tomorrow
		mosmix_t *m1 = TOMORROW(h);
		expect(m1, f);
		errors(m1);

		// history
		for (int d = 0; d < 7; d++) {
			mosmix_t *m = HISTORY(d, h);
			expect(m, f);
			errors(m);
		}
	}
}

static void history_total() {
	mosmix_t t;
	ZERO(t);
	xlog("MOSMIX history total");
	for (int h = 0; h < 24; h++) {
		mosmix_t th;
		ZERO(th);

		for (int d = 0; d < 7; d++) {
			mosmix_t *m = HISTORY(d, h);
			sum_abs(&th, m);
		}
		sum_abs(&t, &th);

		if (th.diff1 || th.diff2 || th.diff3 || th.diff4)
			xlog("h=%2d diff1=%4d  diff2=%4d  diff3=%4d  diff4=%4d", h, th.diff1, th.diff2, th.diff3, th.diff4);
	}
	int total = t.diff1 + t.diff2 + t.diff3 + t.diff4;
	xlog("sum       %5d       %5d       %5d       %5d   total=%d", t.diff1, t.diff2, t.diff3, t.diff4, total);
}

// update today and tomorrow with actual data from mosmix kml download
static void update_today_tomorrow(struct tm *now) {
	struct tm tm;
	int day_today = now->tm_yday;
	int day_tomorrow = now->tm_yday < 364 ? now->tm_yday + 1 : 0;

	// loop over one week
	for (int i = 0; i < HISTORY_SIZE; i++) {
		csv_t *mcsv = &mosmix_csv[i];

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

		// SunD1 without Rad1h is not possible
		if (!mcsv->Rad1h)
			mcsv->SunD1 = 0;

		// update
		if (m->Rad1h != mcsv->Rad1h) {
			xlog("MOSMIX updating %02d.%02d. %02d:00 Rad1h %3d <-- %3d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, m->Rad1h, mcsv->Rad1h);
			m->Rad1h = mcsv->Rad1h;
		}
		if (m->SunD1 != mcsv->SunD1) {
			xlog("MOSMIX updating %02d.%02d. %02d:00 SunD1 %3d <-- %3d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, m->SunD1, mcsv->SunD1);
			m->SunD1 = mcsv->SunD1;
		}
		if (m->TTT != (int) mcsv->TTT) {
			xlog("MOSMIX updating %02d.%02d. %02d:00   TTT %3d <-- %3d", tm.tm_mday, tm.tm_mon + 1, tm.tm_hour, m->TTT, (int) mcsv->TTT);
			m->TTT = mcsv->TTT;
		}
	}
}

// Rad1h factors determination by averaging mosmix history actual vs. expected per mppt
static void calculate_factors() {
	ZERO(factors);

	for (int h = 0; h < 24; h++) {
		factor_t fd[7];
		ZERO(fd);

		// calculate 7 factors
		for (int d = 0; d < 7; d++) {
			mosmix_t *m = HISTORY(d, h);
			factor(m, &fd[d]);
		}

		// calculate average factor
		factor_t *f = FACTOR(h);
		ZEROP(f);
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

//		if (f->r1)
//			xlog("MOSMIX MPPT1 h=%2d 1=%3d 2=%3d 3=%3d 4=%3d 5=%3d 6=%3d 7=%3d fr=%3d", h, fd[0].r1, fd[1].r1, fd[2].r1, fd[3].r1, fd[4].r1, fd[5].r1, fd[6].r1, f->r1);
	}

	store_table_csv(factors, FACTOR_SIZE, 24, FACTOR_HEADER, RUN SLASH MOSMIX_FACTORS_CSV);
//	dump_table(factors, FACTOR_SIZE, 24, 0, "MOSMIX factors", FACTOR_HEADER);
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
	int exp1 = round100(SUM_EXP(&mtoday));
	int sodx1 = round100(SUM_EXP(&msod));
	int sodm1 = SUM_MPPT(&msod);
	*succ1 = sodx1 ? sodm1 * 1000 / sodx1 : 0;

	// nothing to scale
	if (TODAY(now->tm_hour)->Rad1h == 0)
		return;

	mosmix_t m;
	ZERO(m);

	// sum up mppt and expected till now
	for (int h = 0; h < CH; h++)
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
	int exp2 = round100(SUM_EXP(&mtoday));
	int sodx2 = round100(SUM_EXP(&msod));
	int sodm2 = SUM_MPPT(&msod);
	*succ2 = sodx2 ? sodm2 * 1000 / sodx2 : 0;

	float fs1 = FLOAT10(*succ1), fs2 = FLOAT10(*succ2);
	xlog("MOSMIX scaling   before: total=%d mppt=%d exp=%d succ=%.1f%%   after: total=%d mppt=%d exp=%d succ=%.1f%%", exp1, sodm1, sodx1, fs1, exp2, sodm2, sodx2, fs2);
}

void mosmix_marble(struct tm *now, const int baseload, const int heating, marble_t *marbles) {
	ZEROP(marbles);

	// collect total expected today, tomorrow and till end of day / start of day
	mosmix_t mtoday, mtomorrow, msod, meod;
	collect(now, &mtomorrow, &mtoday, &msod, &meod);

	marbles->tomorrow = round100(SUM_EXP(&mtomorrow));
	marbles->today = round100(SUM_EXP(&mtoday));
	marbles->sod = round100(SUM_EXP(&msod));
	marbles->eod = round100(SUM_EXP(&meod));
	xdebug("MOSMIX tomorrow=%d today=%d sod=%d eod=%d", marbles->tomorrow, marbles->today, marbles->sod, marbles->eod);

	// look ahead and calculate minutes and power needed by day, to survive next night and to heat
	calculate_power(baseload, heating);
	collect_day(now, &marbles->day_mins, &marbles->day);
	collect_night(now, &marbles->night_mins, &marbles->night);
	collect_heating(now, &marbles->heat_mins, &marbles->heat);
}

void mosmix_update_akku_load(int h, int a, int l) {
	akku[h] = a;
	load[h] = l;
}

void mosmix_dump_today(struct tm *now) {
	mosmix_t m;
	icumulate(&m, today, MOSMIX_SIZE, 24);
	dump_table(today, MOSMIX_SIZE, 24, CH, "MOSMIX today", MOSMIX_HEADER);
	dump_array(&m, MOSMIX_SIZE, "[++]", 0);
}

void mosmix_dump_tomorrow(struct tm *now) {
	mosmix_t m;
	icumulate(&m, tomorrow, MOSMIX_SIZE, 24);
	dump_table(tomorrow, MOSMIX_SIZE, 24, CH, "MOSMIX tomorrow", MOSMIX_HEADER);
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
	load_blob(STATE SLASH MOSMIX_TOMORROW, tomorrow, sizeof(tomorrow));
	load_blob(STATE SLASH MOSMIX_TODAY, today, sizeof(today));
}

void mosmix_store_state() {
	store_blob(STATE SLASH MOSMIX_HISTORY, history, sizeof(history));
	store_blob(STATE SLASH MOSMIX_TOMORROW, tomorrow, sizeof(tomorrow));
	store_blob(STATE SLASH MOSMIX_TODAY, today, sizeof(today));
}

void mosmix_store_csv() {
	store_table_csv(factors, FACTOR_SIZE, 24, FACTOR_HEADER, RUN SLASH MOSMIX_FACTORS_CSV);
	store_table_csv(history, MOSMIX_SIZE, HISTORY_SIZE, MOSMIX_HEADER, RUN SLASH MOSMIX_HISTORY_CSV);
	store_table_csv(tomorrow, MOSMIX_SIZE, 24, MOSMIX_HEADER, RUN SLASH MOSMIX_TOMORROW_CSV);
	store_table_csv(today, MOSMIX_SIZE, 24, MOSMIX_HEADER, RUN SLASH MOSMIX_TODAY_CSV);
}

int mosmix_load_csv(struct tm *now, const char *station, int clear) {
	char filename[64];
	char *column[8];
	char buf[LINEBUF];

	snprintf(filename, 64, TMP SLASH "%s" CSV, station);
	FILE *fp = fopen(filename, "rt");
	if (fp == NULL)
		return xerr("MOSMIX Cannot open file %s for reading", filename);

	ZERO(mosmix_csv);

	// header
	if (fgets(buf, LINEBUF, fp) == NULL)
		return xerr("MOSMIX no data available");

	int lines = 0;
	while (fgets(buf, LINEBUF, fp) != NULL) {
		int i = 0;
		char *p = strtok(buf, ",");
		while (p != NULL) {
			column[i++] = p;
			p = strtok(NULL, ",");
		}
		parse(column, ARRAY_SIZE(column));
		lines++;
	}

	fclose(fp);
	xlog("MOSMIX loaded %4d lines from %s", lines, filename);

	if (clear) {
		ZERO(today);
		ZERO(tomorrow);
	}

	update_today_tomorrow(now);
	calculate_factors();
	recalc_expected();

	return 0;
}

int mosmix_load_json(struct tm *now, const char *station, int clear) {
	char filename[64];
	struct json_token t;

	ZERO(mosmix_csv);

	// read time stamp file
	snprintf(filename, 64, TMP SLASH "mosmix-timestamps.json");
	char *timestamps = json_fread(filename);
	if (timestamps == NULL)
		return xerr("MOSMIX Cannot open file %s for reading", filename);

	for (int i = 0; json_scanf_array_elem(timestamps, strlen(timestamps), "", i, &t) > 0; i++)
		sscanf(t.ptr, "%ld", &mosmix_csv[i].ts);

	free(timestamps);

	// read forecast file
	snprintf(filename, 64, TMP SLASH "mosmix-forecasts-%s.json", station);
	char *forecasts = json_fread(filename);
	if (forecasts == NULL)
		return xerr("MOSMIX Cannot open file %s for reading", filename);

	char *rad1h = strstr(forecasts, "\"Rad1h\":") + 9;
	for (int i = 0; json_scanf_array_elem(rad1h, strlen(rad1h), "", i, &t) > 0; i++)
		sscanf(t.ptr, "%d", &mosmix_csv[i].Rad1h);

	char *sund1 = strstr(forecasts, "\"SunD1\":") + 9;
	for (int i = 0; json_scanf_array_elem(sund1, strlen(sund1), "", i, &t) > 0; i++)
		sscanf(t.ptr, "%d", &mosmix_csv[i].SunD1);

	char *ttt = strstr(forecasts, "\"TTT\":") + 7;
	for (int i = 0; json_scanf_array_elem(ttt, strlen(ttt), "", i, &t) > 0; i++)
		sscanf(t.ptr, "%f", &mosmix_csv[i].TTT);

	free(forecasts);

	// post process data
	int count = 0;
	for (int i = 0; i < 256; i++) {
		csv_t *csv = &mosmix_csv[i];

		if (csv->ts)
			count++;

		csv->TTT -= 273.15; // convert to °C
		csv->SunD1 = csv->SunD1 * 100 / 3600; // convert to 0..100 percent
	}

	xlog("MOSMIX loaded %4d entries for station %s", count, station);

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
	marble_t mosmix_marbles, *marbles = &mosmix_marbles;

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
	// mosmix_load_csv(now, MARIENBERG, 0);
	mosmix_load_json(now, MARIENBERG, 0);
	mosmix_dump_today(now);
	mosmix_dump_tomorrow(now);

	mosmix_marble(now, BASELOAD, 2000, marbles);

//	int itoday, itomorrow, sod, eod, eodh, succ1, succ2;
//
//	// calculate expected today and tomorrow
//	xlog("MOSMIX *** now (%02d) ***", now->tm_hour);
//	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod, &eodh);
//	mosmix_dump_today(now);
//	mosmix_dump_tomorrow(now);
//
//	xlog("MOSMIX *** updated now (%02d) ***", now->tm_hour);
//	mosmix_mppt(now, 4000, 3000, 2000, 1000);
//	mosmix_scale(now, &succ1, &succ2);
//	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod, &eodh);
//
//	now->tm_hour = 9;
//	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
//	mosmix_mppt(now, 4000, 3000, 2000, 1000);
//	mosmix_scale(now, &succ1, &succ2);
//	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod, &eodh);
//
//	now->tm_hour = 12;
//	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
//	mosmix_mppt(now, 4000, 3000, 2000, 1000);
//	mosmix_scale(now, &succ1, &succ2);
//	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod, &eodh);
//
//	now->tm_hour = 15;
//	xlog("MOSMIX *** updated hour %02d ***", now->tm_hour);
//	mosmix_mppt(now, 4000, 3000, 2000, 1000);
//	mosmix_scale(now, &succ1, &succ2);
//	mosmix_collect(now, &itomorrow, &itoday, &sod, &eod, &eodh);
//
//	mosmix_dump_history(now);
//	mosmix_dump_history_hours(9);
//	mosmix_dump_history_hours(12);
//	mosmix_dump_history_hours(15);

//	now->tm_hour = 7;
//	now->tm_min = 40;

//	mosmix_dump_history_hours(8);

	return 0;
}

static int recalc() {
	LOCALTIME

	mosmix_load_state(now);
	mosmix_load_csv(now, MARIENBERG, 0);
	calculate_factors();
	recalc_expected();
	history_total();
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

	// fix wrong values in history - copy from yesterday
	int wrong = 161, copy = wrong > 24 ? (wrong - 24) : (wrong + 24);
	history[wrong].mppt1 = history[copy].mppt1;
	history[wrong].mppt2 = history[copy].mppt2;
	history[wrong].mppt3 = history[copy].mppt3;
	history[wrong].mppt4 = history[copy].mppt4;

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
		factor_t *f = FACTOR(h);
		expect(m, f);
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
	mosmix_load_csv(yday, MARIENBERG, 1);
	int dm = diffs(yday);

	wget(yday, "10577");
	mosmix_load_csv(yday, CHEMNITZ, 1);
	int dc = diffs(yday);

	wget(yday, "N4464");
	mosmix_load_csv(yday, BRAUNSDORF, 1);
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
