/**
 * !!! Warning !!! deprecated unmaintained and untested code, use fronius-modbus.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <ansi-color-codes.h>

#include "fronius-config.h"
#include "fronius.h"
#include "tasmota.h"
#include "frozen.h"
#include "mosmix.h"
#include "utils.h"
#include "curl.h"
#include "mcp.h"

// hexdump -v -e '1 "%10d " 3 "%8d ""\n"' /work/fronius-minmax.bin
#define MINMAX_FILE				"/work/fronius-minmax.bin"

#define COUNTER_HISTORY		30		// days
#define PSTATE_HISTORY		32		// samples

#define URL_READABLE		"http://fronius/components/readable"
#define URL_METER			"http://fronius/solar_api/v1/GetMeterRealtimeData.cgi?Scope=Device&DeviceId=0"
#define URL_FLOW10			"http://fronius10/solar_api/v1/GetPowerFlowRealtimeData.fcgi"
#define URL_FLOW7			"http://fronius7/solar_api/v1/GetPowerFlowRealtimeData.fcgi"

#define WAIT_OFFLINE		900
#define WAIT_STANDBY		300
#define WAIT_STABLE			60
#define WAIT_INSTABLE		20
#define WAIT_AKKU			10
#define WAIT_NEXT			5
#define WAIT_RAMP			3

#define JMC					" SMARTMETER_ENERGYACTIVE_CONSUMED_SUM_F64:%f "
#define JMP					" SMARTMETER_ENERGYACTIVE_PRODUCED_SUM_F64:%f "
#define JMV1				" SMARTMETER_VOLTAGE_MEAN_01_F64:%f "
#define JMV2				" SMARTMETER_VOLTAGE_MEAN_02_F64:%f "
#define JMV3				" SMARTMETER_VOLTAGE_MEAN_03_F64:%f "
#define JMF					" SMARTMETER_FREQUENCY_MEAN_F64:%f "

#define JBSOC				" BAT_VALUE_STATE_OF_CHARGE_RELATIVE_U16:%f "

#define JIE1				" PV_ENERGYACTIVE_ACTIVE_SUM_01_U64:%f "
#define JIE2				" PV_ENERGYACTIVE_ACTIVE_SUM_02_U64:%f "
#define JIP					" PV_POWERACTIVE_SUM_F64:%f "

#define JMMPP				" PowerReal_P_Sum:%f "
#define JMMC				" EnergyReal_WAC_Sum_Consumed:%f "
#define JMMP				" EnergyReal_WAC_Sum_Produced:%f "

typedef struct _raw raw_t;

struct _raw {
	float akku;
	float grid;
	float load;
	float pv10;
	float pv10_total1;
	float pv10_total2;
	float pv7;
	float pv7_total;
	float soc;
	float produced;
	float consumed;
	float p;
	float v1;
	float v2;
	float v3;
	float f;
};

// program of the day - choose by mosmix forecast data
static potd_t *potd = 0;

// counter history
static counter_t counter_history[COUNTER_HISTORY], *counter = &counter_history[0];
static int counter_history_ptr = 0;

// power state with actual power flow and state calculations
static pstate_t pstate_history[PSTATE_HISTORY], *pstate = &pstate_history[0];
static int pstate_history_ptr = 0;

// global state with total counters and daily calculations
static gstate_t gstate_history[24], *gstate = &gstate_history[0];

// storage for holding minimum and maximum voltage values
static minmax_t mm, *minmax = &mm;

static struct tm *lt, now_tm, *now = &now_tm;
static int sock = 0;

// reading Inverter API: CURL handles, response memory, raw date, error counter
static CURL *curl10, *curl7, *curl_readable;
static response_t memory = { 0 };
static raw_t raw, *r = &raw;
static int errors;

int set_heater(device_t *heater, int power) {
	// fix power value if out of range
	if (power < 0)
		power = 0;
	if (power > 1)
		power = 1;

	if (heater->override) {
		time_t t = time(NULL);
		if (t > heater->override) {
			xdebug("FRONIUS Override expired for %s", heater->name);
			heater->override = 0;
			power = 0;
		} else {
			xdebug("FRONIUS Override active for %lu seconds on %s", heater->override - t, heater->name);
			power = 100;
		}
	}

	// check if update is necessary
	if (heater->power && heater->power == power)
		return 0;

#ifndef FRONIUS_MAIN
	if (power)
		tasmota_power(heater->id, heater->r, 1);
	else
		tasmota_power(heater->id, heater->r, 0);
#endif

	// update power values
	heater->power = power;
	heater->load = power ? heater->total : 0;
	heater->xload = power ? heater->total * -1 : heater->total;
	return 1; // loop done
}

// echo p:0:0 | socat - udp:boiler3:1975
// for i in `seq 1 10`; do let j=$i*10; echo p:$j:0 | socat - udp:boiler1:1975; sleep 1; done
int set_boiler(device_t *boiler, int power) {
	// fix power value if out of range
	if (power < 0)
		power = 0;
	if (power > 100)
		power = 100;

	if (boiler->override) {
		time_t t = time(NULL);
		if (t > boiler->override) {
			xdebug("FRONIUS Override expired for %s", boiler->name);
			boiler->override = 0;
			power = 0;
		} else {
			xdebug("FRONIUS Override active for %lu seconds on %s", boiler->override - t, boiler->name);
			power = 100;
		}
	}

	// no update necessary
	if (boiler->power && boiler->power == power)
		return 0; // continue loop

	// can we send a message
	if (boiler->addr == NULL)
		return 0; // continue loop

	// calculate step
	int step = power - boiler->power;

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock == 0)
		return xerr("Error creating socket");

	// write IP and port into sockaddr structure
#ifndef FRONIUS_MAIN
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(boiler->addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	// send message to boiler
	char message[16];
	snprintf(message, 16, "p:%d:%d", power, 0);
	int ret = sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	if (ret < 0)
		return xerr("Sendto failed on %s %s", boiler->addr, strerror(ret));
	if (step < 0) {
		xdebug("FRONIUS ramp↓ %s step %d UDP %s", boiler->name, step, message);
	} else
		xdebug("FRONIUS ramp↑ %s step +%d UDP %s", boiler->name, step, message);
#endif

	// update power values
	boiler->power = power;
	boiler->load = boiler->total * boiler->power / 100;
	boiler->xload = boiler->total * step / -100;
	return 1; // loop done
}

static void set_all_devices(int power) {
	for (device_t **d = DEVICES; *d != 0; d++)
		((*d)->set_function)(*d, power);
}

// initialize all devices with start values
static void init_all_devices() {
	for (device_t **dd = DEVICES; *dd != 0; dd++) {
		device_t *d = *dd;
		d->addr = resolve_ip(d->name);
		d->state = Active;
		d->power = -1; // force set to 0
		(d->set_function)(d, 0);
		if (d->adjustable && d->addr == 0)
			d->state = Disabled; // controlled via socket send, so we need an ip address
	}
}

static counter_t* get_counter_history(int offset) {
	int i = counter_history_ptr + offset;
	if (i < 0)
		i += COUNTER_HISTORY;
	if (i >= COUNTER_HISTORY)
		i -= COUNTER_HISTORY;
	return &counter_history[i];
}

static pstate_t* get_pstate_history(int offset) {
	int i = pstate_history_ptr + offset;
	if (i < 0)
		i += PSTATE_HISTORY;
	if (i >= PSTATE_HISTORY)
		i -= PSTATE_HISTORY;
	return &pstate_history[i];
}

static void dump_counter(int back) {
	char line[sizeof(counter_t) * 12 + 16], value[16];

	strcpy(line, "FRONIUS counter   idx         ts       pv10        pv7      ↑grid      ↓grid");
	xdebug(line);
	for (int y = 0; y < back; y++) {
		strcpy(line, "FRONIUS counter ");
		snprintf(value, 16, "[%3d] ", y * -1);
		strcat(line, value);
		int *vv = (int*) get_counter_history(y * -1);
		for (int x = 0; x < sizeof(counter_t) / sizeof(int); x++) {
			snprintf(value, 12, "%10d ", vv[x]);
			strcat(line, value);
		}
		xdebug(line);
	}
}

static void dump_gstate() {
	char line[sizeof(pstate_t) * 12 + 16], value[16];
	int highlight = now->tm_hour > 0 ? now->tm_hour - 1 : 23;

	strcpy(line, "FRONIUS gstate   idx     pv   pv10    pv7  ↑grid  ↓grid  today   tomo    sun    exp   load    soc   akku  dakku   duty    ttl   mosm   surv");
	xdebug(line);
	for (int y = 0; y < 24; y++) {
		strcpy(line, "FRONIUS gstate ");
		if (y == highlight)
			strcat(line, BOLD);
		snprintf(value, 16, "[%3d] ", y);
		strcat(line, value);
		int *vv = (int*) &gstate_history[y];
		for (int x = 0; x < sizeof(gstate_t) / sizeof(int); x++) {
			snprintf(value, 12, "%6d ", vv[x]);
			strcat(line, value);
		}
		if (y == highlight)
			strcat(line, RESET);
		xdebug(line);
	}
}

static void dump_pstate(int back) {
	char line[sizeof(pstate_t) * 8 + 16], value[16];

	strcpy(line, "FRONIUS pstate  idx    pv   Δpv   grid Δgrid  akku  ac10   ac7  load Δload xload dxlod  dc10  10.1  10.2   dc7   7.1   7.2  surp  grdy modst  tend  wait   soc");
	xdebug(line);
	for (int y = 0; y < back; y++) {
		strcpy(line, "FRONIUS pstate ");
		snprintf(value, 16, "[%2d] ", y * -1);
		strcat(line, value);
		int *vv = (int*) get_pstate_history(y * -1);
		for (int x = 0; x < sizeof(pstate_t) / sizeof(int) - 1; x++) {
			snprintf(value, 8, x == 2 ? "%6d " : "%5d ", vv[x]);
			strcat(line, value);
		}
		xdebug(line);
	}
}

static void bump_counter(time_t now_ts) {
	// calculate new counter pointer
	if (++counter_history_ptr == COUNTER_HISTORY)
		counter_history_ptr = 0;
	counter_t *counter_new = &counter_history[counter_history_ptr];

	// take over all values
	memcpy(counter_new, (void*) counter, sizeof(counter_t));
	counter = counter_new; // atomic update current counter pointer
}

static void bump_pstate() {
	// calculate new pstate pointer
	if (++pstate_history_ptr == PSTATE_HISTORY)
		pstate_history_ptr = 0;
	pstate_t *pstate_new = &pstate_history[pstate_history_ptr];

	// take over all values
	memcpy(pstate_new, (void*) pstate, sizeof(pstate_t));
	pstate = pstate_new; // atomic update current pstate pointer
}

static void print_dstate(int wait) {
	char message[128], value[5];

	strcpy(message, "FRONIUS device power ");
	for (device_t **d = DEVICES; *d != 0; d++) {
		if ((*d)->adjustable)
			snprintf(value, 5, " %3d", (*d)->power);
		else
			snprintf(value, 5, "   %c", (*d)->power ? 'X' : '_');
		strcat(message, value);
	}

	strcat(message, "   state ");
	for (device_t **d = DEVICES; *d != 0; d++) {
		snprintf(value, 5, "%d", (*d)->state);
		strcat(message, value);
	}

	strcat(message, "   noresponse ");
	for (device_t **d = DEVICES; *d != 0; d++) {
		snprintf(value, 5, "%d", (*d)->noresponse);
		strcat(message, value);
	}

	strcat(message, "   wait ");
	snprintf(value, 5, "%d", wait);
	strcat(message, value);

	xlog(message);
}

static void print_gstate(const char *message) {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS gstate");
	xlogl_int_b(line, "PV", gstate->pv);
	xlogl_int_b(line, "PV10", gstate->pv10);
	xlogl_int_b(line, "PV7", gstate->pv7);
	xlogl_int(line, 1, 0, "↑Grid", gstate->produced);
	xlogl_int(line, 1, 1, "↓Grid", gstate->consumed);
	xlogl_int(line, 0, 0, "Sun", gstate->sun);
	xlogl_int(line, 0, 0, "Today", gstate->today);
	xlogl_int(line, 0, 0, "Tomo", gstate->tomorrow);
	xlogl_int(line, 0, 0, "Exp", gstate->expected);
	xlogl_float(line, 0, 0, "SoC", FLOAT10(gstate->soc));
	xlogl_int(line, 0, 0, "Akku", gstate->akku);
	xlogl_float(line, 0, 0, "TTL", FLOAT60(gstate->ttl));
	xlogl_float(line, 0, 0, "Mosmix", FLOAT10(gstate->mosmix));
	xlogl_float(line, 1, gstate->survive < 10, "Survive", FLOAT10(gstate->survive));
	strcat(line, " potd:");
	strcat(line, potd ? potd->name : "NULL");
	xlogl_end(line, strlen(line), message);
}

static void print_pstate(const char *message) {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS pstate");
	xlogl_int_b(line, "PV", pstate->pv);
	xlogl_int_b(line, "PV10", pstate->pv10_1 + pstate->pv10_2);
	xlogl_int_b(line, "PV7", pstate->pv7_1 + pstate->pv7_2);
	xlogl_int(line, 1, 1, "Grid", pstate->grid);
	xlogl_int(line, 1, 1, "Akku", pstate->akku);
	xlogl_int(line, 1, 0, "Surp", pstate->surplus);
	xlogl_int(line, 1, 0, "Greedy", pstate->greedy);
	xlogl_int(line, 1, 0, "Modest", pstate->modest);
	xlogl_int(line, 0, 0, "Load", pstate->load);
	xlogl_int(line, 0, 0, "ΔLoad", pstate->dload);
	xlogl_float(line, 0, 0, "SoC", FLOAT10(pstate->soc));
	xlogl_bits(line, "Flags", pstate->flags);
	xlogl_end(line, strlen(line), message);
}

static void print_minimum_maximum() {
	char line[512]; // 256 is not enough due to color escape sequences!!!
	xlogl_start(line, "FRONIUS");
	strcat(line, " Minima:");
	xlogl_float_b(line, "V1", FLOAT10(minmax->v1min));
	xlogl_float(line, 0, 0, "V2", FLOAT10(minmax->v12min));
	xlogl_float(line, 0, 0, "V3", FLOAT10(minmax->v13min));
	xlogl_float_b(line, "V2", FLOAT10(minmax->v2min));
	xlogl_float(line, 0, 0, "V1", FLOAT10(minmax->v21min));
	xlogl_float(line, 0, 0, "V3", FLOAT10(minmax->v23min));
	xlogl_float_b(line, "V3", FLOAT10(minmax->v3min));
	xlogl_float(line, 0, 0, "V1", FLOAT10(minmax->v31min));
	xlogl_float(line, 0, 0, "V2", FLOAT10(minmax->v32min));
	xlogl_float_b(line, "Hz", FLOAT10(minmax->fmin));
	strcat(line, "   Maxima:");
	xlogl_float_b(line, "V1", FLOAT10(minmax->v1max));
	xlogl_float(line, 0, 0, "V2", FLOAT10(minmax->v12max));
	xlogl_float(line, 0, 0, "V3", FLOAT10(minmax->v13max));
	xlogl_float_b(line, "V2", FLOAT10(minmax->v2max));
	xlogl_float(line, 0, 0, "V1", FLOAT10(minmax->v21max));
	xlogl_float(line, 0, 0, "V3", FLOAT10(minmax->v23max));
	xlogl_float_b(line, "V3", FLOAT10(minmax->v3max));
	xlogl_float(line, 0, 0, "V1", FLOAT10(minmax->v31max));
	xlogl_float(line, 0, 0, "V2", FLOAT10(minmax->v32max));
	xlogl_float_b(line, "Hz", FLOAT10(minmax->fmax));
	xlogl_end(line, strlen(line), NULL);
}

static void minimum_maximum_store(time_t now_ts, int *ts, int *v1, int *v2, int *v3) {
	*ts = now_ts;
	*v1 = r->v1 * 10.0;
	*v2 = r->v2 * 10.0;
	*v3 = r->v3 * 10.0;
}

static void minimum_maximum(time_t now_ts) {
	if (r->v1 * 10.0 < minmax->v1min)
		minimum_maximum_store(now_ts, &minmax->v1min_ts, &minmax->v1min, &minmax->v12min, &minmax->v13min);
	if (r->v2 * 10.0 < minmax->v2min)
		minimum_maximum_store(now_ts, &minmax->v2min_ts, &minmax->v21min, &minmax->v2min, &minmax->v23min);
	if (r->v3 * 10.0 < minmax->v3min)
		minimum_maximum_store(now_ts, &minmax->v3min_ts, &minmax->v31min, &minmax->v32min, &minmax->v3min);
	if (r->v1 * 10.0 > minmax->v1max)
		minimum_maximum_store(now_ts, &minmax->v1max_ts, &minmax->v1max, &minmax->v12max, &minmax->v13max);
	if (r->v2 * 10.0 > minmax->v2max)
		minimum_maximum_store(now_ts, &minmax->v2max_ts, &minmax->v21max, &minmax->v2max, &minmax->v23max);
	if (r->v3 * 10.0 > minmax->v3max)
		minimum_maximum_store(now_ts, &minmax->v3max_ts, &minmax->v31max, &minmax->v32max, &minmax->v3max);

	if (r->f * 10.0 < minmax->fmin) {
		minmax->fmin_ts = now_ts;
		minmax->fmin = r->f * 10.0;
	}
	if (r->f * 10.0 > minmax->fmax) {
		minmax->fmax_ts = now_ts;
		minmax->fmax = r->f * 10.0;
	}
}

static int parse_fronius10(response_t *resp) {
	char *c;
	int ret;

	ret = json_scanf(resp->buffer, resp->size, "{ Body { Data { Site { P_Akku:%f P_Grid:%f P_Load:%f P_PV:%f } } } }", &r->akku, &r->grid, &r->load, &r->pv10);
	if (ret != 4)
		xlog("FRONIUS parse_fronius10() warning! parsing Body->Data->Site: expected 4 values but got %d", ret);

	// workaround parsing { "Inverters" : { "1" : { ... } } }
	ret = json_scanf(resp->buffer, resp->size, "{ Body { Data { Inverters:%Q } } }", &c);
	if (ret == 1 && c != NULL) {
		char *p = c;
		while (*p != '{')
			p++;
		p++;
		while (*p != '{')
			p++;

		ret = json_scanf(p, strlen(p) - 1, "{ SOC:%f }", &r->soc);
		if (ret != 1)
			xlog("FRONIUS parse_fronius10() warning! parsing Body->Data->Inverters->SOC: no result");

		free(c);
	} else
		xlog("FRONIUS parse_fronius10() warning! parsing Body->Data->Inverters: no result");

	return 0;
}

static int parse_fronius7(response_t *resp) {
	int ret = json_scanf(resp->buffer, resp->size, "{ Body { Data { Site { P_PV:%f E_Total:%f } } } }", &r->pv7, &r->pv7_total);
	if (ret != 2)
		return xerr("FRONIUS parse_fronius7() warning! parsing Body->Data->Site: expected 2 values but got %d", ret);

	return 0;
}

//static int parse_meter(response_t *resp) {
//	int ret = json_scanf(resp->buffer, resp->size, "{ Body { Data { "JMMPP JMMC JMMP" } } }", &r->p, &r->consumed, &r->produced);
//	if (ret != 3)
//		return xerr("FRONIUS parse_meter() warning! parsing Body->Data: expected 3 values but got %d", ret);
//
//	return 0;
//}

static int parse_readable(response_t *resp) {
	int ret;
	char *p;

	// workaround for accessing inverter number as key: "262144" : {
	p = strstr(resp->buffer, "\"262144\"") + 8 + 2;
	ret = json_scanf(p, strlen(p), "{ channels { "JIP" } }", &r->pv10);
	if (ret != 1)
		return xerr("FRONIUS parse_readable() warning! parsing 262144: expected 1 values but got %d", ret);

	// workaround for accessing inverter number as key: "393216" : {
	p = strstr(resp->buffer, "\"393216\"") + 8 + 2;
	ret = json_scanf(p, strlen(p), "{ channels { "JIE1 JIE2" } }", &r->pv10_total1, &r->pv10_total2);
	if (ret != 2)
		return xerr("FRONIUS parse_readable() warning! parsing 393216: expected 2 values but got %d", ret);

	// workaround for accessing akku number as key: "16580608" : {
	p = strstr(resp->buffer, "\"16580608\"") + 10 + 2;
	ret = json_scanf(p, strlen(p), "{ channels { "JBSOC" } }", &r->soc);
	if (ret != 1)
		return xerr("FRONIUS parse_readable() warning! parsing 16580608: expected 1 values but got %d", ret);

	// workaround for accessing smartmeter number as key: "16252928" : {
	p = strstr(resp->buffer, "\"16252928\"") + 10 + 2;
	ret = json_scanf(p, strlen(p), "{ channels { "JMC JMP JMV1 JMV2 JMV3 JMF" } }", &r->consumed, &r->produced, &r->v1, &r->v2, &r->v3, &r->f);
	if (ret != 6)
		return xerr("FRONIUS parse_readable() warning! parsing 16252928: expected 6 values but got %d", ret);

	return 0;
}

static int select_program(const potd_t *p) {
	xlog("FRONIUS selecting %s program of the day", p->name);
	if (potd != p) {
		potd = (potd_t*) p;

		// potd has changed - reset all devices
		set_all_devices(0);

		// mark devices greedy/modest
		for (device_t **d = potd->greedy; *d != 0; d++)
			(*d)->greedy = 1;
		for (device_t **d = potd->modest; *d != 0; d++)
			(*d)->greedy = 0;
	}
	return 0;
}

// choose program of the day
static int choose_program() {

	// charging akku has priority
	if (gstate->soc < 100)
		return select_program(&CHARGE);

	// we will NOT survive - charging akku has priority
	if (gstate->survive < 10)
		return select_program(&MODEST);

	// survive and less than 1h sun
	if (gstate->sun < 3600)
		return select_program(&MODEST);

	// tomorrow more pv than today - charge akku tommorrow
	if (gstate->tomorrow > gstate->today)
		return select_program(&GREEDY);

	// enough pv available
	return select_program(&SUNNY);
}

// minimum available power for ramp up
static int rampup_min(device_t *d) {
	int min = d->adjustable ? d->total / 100 : d->total; // adjustable: 1% of total, dumb: total
	if (pstate->soc < 1000)
		min += min / 10; // 10% more while akku is charging to avoid excessive grid load
	if (PSTATE_DISTORTION)
		min *= 2; // 100% more on distortion
	return min;
}

static int calculate_step(device_t *d, int power) {
	// power steps
	int step = power / (d->total / 100);

	if (!step)
		return 0;

	// do smaller up steps / bigger down steps when we have distortion or akku is not yet full (give time to adjust)
	if (PSTATE_DISTORTION || pstate->soc < 1000) {
		if (step > 1)
			step /= 2;
		if (step < -1)
			step *= 2;
	}

	return step;
}

static int ramp_adjustable(device_t *d, int power) {
	// already full up
	if (d->power == 100 && power > 0)
		return 0;

	// already full down
	if (!d->power && power < 0)
		return 0;

	xdebug("FRONIUS ramp_adjustable() %s %d", d->name, power);
	int step = calculate_step(d, power);
	if (!step)
		return 0;

	return (d->set_function)(d, d->power + step);
}

static int ramp_dumb(device_t *d, int power) {
	// keep on as long as we have enough power and device is already on
	if (d->power && power > 0)
		return 0; // continue loop

	int min = rampup_min(d);
	xdebug("FRONIUS ramp_dumb() %s %d (min %d)", d->name, power, min);

	// switch on when enough power is available
	if (!d->power && power > min)
		return (d->set_function)(d, 1);

	// switch off
	if (d->power)
		return (d->set_function)(d, 0);

	return 0; // continue loop
}

static int ramp_device(device_t *d, int power) {
	if (d->state == Disabled || d->state == Standby)
		return 0; // continue loop

	if (d->adjustable)
		return ramp_adjustable(d, power);
	else
		return ramp_dumb(d, power);
}

static device_t* rampup(int power, device_t **devices) {
	for (device_t **d = devices; *d != 0; d++)
		if (ramp_device(*d, power))
			return *d;

	return 0; // next priority
}

static device_t* rampdown(int power, device_t **devices) {
	// jump to last entry
	device_t **d = devices;
	while (*d != 0)
		d++;

	// now go backward - this will give a reverse order
	while (d-- != devices)
		if (ramp_device(*d, power))
			return *d;

	return 0; // next priority
}

static device_t* ramp() {
	device_t *d;

	// prio1: no extra power available: ramp down modest devices
	if (pstate->modest < 0) {
		d = rampdown(pstate->modest, potd->modest);
		if (d)
			return d;
	}

	// prio2: consuming grid power or discharging akku: ramp down greedy devices too
	if (pstate->greedy < 0) {
		d = rampdown(pstate->greedy, potd->greedy);
		if (d)
			return d;
	}

	// ramp up only when state is stable or enough surplus power
	int ok = PSTATE_STABLE || pstate->surplus > 2000;
	if (!ok)
		return 0;

	// prio3: uploading grid power or charging akku: ramp up only greedy devices
	if (pstate->greedy > 0) {
		d = rampup(pstate->greedy, potd->greedy);
		if (d)
			return d;
	}

	// prio4: extra power available: ramp up modest devices too
	if (pstate->modest > 0) {
		d = rampup(pstate->modest, potd->modest);
		if (d)
			return d;
	}

	return 0;
}

static int steal_thief_victim(device_t *t, device_t *v) {
	if (t->state != Active || t->power || !v->load)
		return 0; // thief not active or already on or nothing to steal from victim

	int power = (t->greedy ? pstate->greedy : pstate->modest) + v->load;
	int min = rampup_min(t);
	if (power <= min)
		return 0; // not enough to steal

	xdebug("FRONIUS steal %d from %s %s and provide it to %s %s with a load of %d", v->load, GREEDY_MODEST(v), v->name, GREEDY_MODEST(t), t->name, t->total);
	ramp_device(v, v->load * -1);
	ramp_device(t, power);
	t->xload = 0; // force WAIT but no response expected as we put power from one to another device
	return 1;
}

static device_t* steal() {
	// greedy thief can steal from greedy victims behind
	for (device_t **t = potd->greedy; *t != 0; t++)
		for (device_t **v = t + 1; *v != 0; v++)
			if (steal_thief_victim(*t, *v))
				return *t;

	// greedy thief can steal from all modest victims
	for (device_t **t = potd->greedy; *t != 0; t++)
		for (device_t **v = potd->modest; *v != 0; v++)
			if (steal_thief_victim(*t, *v))
				return *t;

	// modest thief can steal from modest victims behind
	for (device_t **t = potd->modest; *t != 0; t++)
		for (device_t **v = t + 1; *v != 0; v++)
			if (steal_thief_victim(*t, *v))
				return *t;

	return 0;
}

static device_t* perform_standby(device_t *d) {
	d->state = Standby_Check;
	xdebug("FRONIUS starting standby check on %s", d->name);
	if (d->adjustable)
		// do a big ramp
		(d->set_function)(d, d->power + (d->power < 50 ? 25 : -25));
	else
		// toggle
		(d->set_function)(d, d->power ? 0 : 1);
	return d;
}

static int force_standby() {
	if (SUMMER)
		return 1; // summer mode -> off

	// force heating independently from temperature
	if ((now->tm_mon == 4 || now->tm_mon == 8) && now->tm_hour >= 16) // may/sept begin 16 o'clock
		return 0;
	else if ((now->tm_mon == 3 || now->tm_mon == 9) && now->tm_hour >= 14) // apr/oct begin 14 o'clock
		return 0;

	return TEMP_IN > 25; // too hot for heating
}

static device_t* standby() {
	// do we have active devices?
	if (!PSTATE_ACTIVE)
		return 0;

	// put dumb devices into standby if summer or too hot
	if (force_standby(now)) {
		xdebug("FRONIUS month=%d out=%.1f in=%.1f --> forcing standby", now->tm_mon, TEMP_OUT, TEMP_IN);
		for (device_t **dd = DEVICES; *dd != 0; dd++) {
			device_t *d = *dd;
			if (!d->adjustable && d->state == Active) {
				(d->set_function)(d, 0);
				d->state = Standby;
			}
		}
	}

	// no standby check indicated or state is not stable
	if (!PSTATE_CHECK_STANDBY || !PSTATE_STABLE)
		return 0;

	// try first active powered device with noresponse counter > 0
	for (device_t **d = DEVICES; *d != 0; d++)
		if ((*d)->state == Active && (*d)->power && (*d)->noresponse > 0)
			return perform_standby(*d);

	// try first active powered adjustable device
	for (device_t **d = DEVICES; *d != 0; d++)
		if ((*d)->state == Active && (*d)->power && (*d)->adjustable)
			return perform_standby(*d);

	// try first active powered device
	for (device_t **d = DEVICES; *d != 0; d++)
		if ((*d)->state == Active && (*d)->power)
			return perform_standby(*d);

	return 0;
}

static device_t* response(device_t *d) {
	// no delta power - no response to check
	int delta = d->xload;
	if (!delta)
		return 0;

	// reset
	d->xload = 0;

	// ignore response below NOISE
	if (abs(delta) < NOISE) {
		xdebug("FRONIUS ignoring expected response below NOISE %d from %s", delta, d->name);
		d->state = Active;
		return 0;
	}

	// valid response is at least 1/3 of expected
	int response = pstate->dload != 0 && (delta > 0 ? (pstate->dload > delta / 3) : (pstate->dload < delta / 3));

	// response OK
	if (d->state == Active && response) {
		xdebug("FRONIUS response OK from %s, delta load expected %d actual %d", d->name, delta, pstate->dload);
		d->noresponse = 0;
		return 0;
	}

	// standby check was negative - we got a response
	if (d->state == Standby_Check && response) {
		xdebug("FRONIUS standby check negative for %s, delta load expected %d actual %d", d->name, delta, pstate->dload);
		d->noresponse = 0;
		d->state = Active;
		return d; // continue main loop
	}

	// standby check was positive -> set device into standby
	if (d->state == Standby_Check && !response) {
		xdebug("FRONIUS standby check positive for %s, delta load expected %d actual %d --> entering standby", d->name, delta, pstate->dload);
		(d->set_function)(d, 0);
		d->noresponse = 0;
		d->state = Standby;
		d->xload = 0; // no response expected
		return d; // continue main loop
	}

	// ignore standby check when power was released
	if (delta > 0)
		return 0;

	// perform standby check when noresponse counter reaches threshold
	if (++d->noresponse >= STANDBY_NORESPONSE)
		return perform_standby(d);

	xdebug("FRONIUS no response from %s count %d/%d", d->name, d->noresponse, STANDBY_NORESPONSE);
	return 0;
}

static void calculate_mosmix(time_t now_ts) {
	// reload mosmix data
	if (mosmix_load(CHEMNITZ))
		return;

	// gstate of last hour to match till now produced vs. till now predicted
	gstate_t *h = (gstate_t*) (gstate != &gstate_history[0] ? gstate - 1 : &gstate_history[23]);

	// sod+eod - values from midnight to now and now till next midnight
	mosmix_t sod, eod;
	mosmix_sod_eod(now_ts, &sod, &eod);

	// recalculate mosmix factor when we have pv: till now produced vs. till now predicted
	float mosmix;
	if (h->pv && sod.Rad1h) {
		mosmix = (float) h->pv / (float) sod.Rad1h;
		gstate->mosmix = mosmix * 10; // store as x10 scaled
	} else
		mosmix = lround((float) gstate->mosmix / 10.0); // take over existing value

	// expected pv power till end of day
	gstate->expected = eod.Rad1h * mosmix;
	xdebug("FRONIUS mosmix pv=%d sod=%d eod=%d expected=%d mosmix=%.1f", h->pv, sod.Rad1h, eod.Rad1h, gstate->expected, mosmix);

	// mosmix total expected today and tomorrow
	mosmix_t m0, m1;
	mosmix_24h(now_ts, 0, &m0);
	mosmix_24h(now_ts, 1, &m1);
	gstate->today = m0.Rad1h * mosmix;
	gstate->tomorrow = m1.Rad1h * mosmix;
	xdebug("FRONIUS mosmix Rad1h/SunD1 today %d/%d, tomorrow %d/%d, exp today %d exp tomorrow %d", m0.Rad1h, m0.SunD1, m1.Rad1h, m1.SunD1, gstate->today, gstate->tomorrow);

	// calculate survival factor
	int rad1h_min = BASELOAD / mosmix; // minimum value when we can live from pv and don't need akku anymore
	int hours, from, to;
	mosmix_survive(now_ts, rad1h_min, &hours, &from, &to);
	int needed = 0;
	// sum up load for darkness hours - take values from yesterday
	for (int i = from; i < to; i++)
		needed += gstate_history[i].dakku;
	int available = gstate->expected + gstate->akku;
	float survive = needed ? lround((float) available) / ((float) needed) : 0;
	gstate->survive = survive * 10; // store as x10 scaled
	xdebug("FRONIUS mosmix needed=%d available=%d (%d expected + %d akku) survive=%.1f", needed, available, gstate->expected, gstate->akku, survive);
}

static void calculate_gstate() {
	// take over raw values
	gstate->soc = r->soc * 10.0; // store value as promille 0/00
	counter->pv10 = (r->pv10_total1 / 3600) + (r->pv10_total2 / 3600); // counters are in Ws!
	counter->produced = r->produced;
	counter->consumed = r->consumed;
	if (r->pv7_total > 0.0)
		counter->pv7 = r->pv7_total; // don't take over zero as Fronius7 might be in sleep mode

	// calculate daily values - when we have actual values and values from yesterday
	counter_t *y = get_counter_history(-1);
	gstate->produced = !counter->produced || !y->produced ? 0 : counter->produced - y->produced;
	gstate->consumed = !counter->consumed || !y->consumed ? 0 : counter->consumed - y->consumed;
	gstate->pv10 = !counter->pv10 || !y->pv10 ? 0 : counter->pv10 - y->pv10;
	gstate->pv7 = !counter->pv7 || !y->pv7 ? 0 : counter->pv7 - y->pv7;
	gstate->pv = gstate->pv10 + gstate->pv7;

	// get previous gstate slot to calculate deltas
	gstate_t *h = (gstate_t*) (gstate != &gstate_history[0] ? gstate - 1 : &gstate_history[23]);
	// xdebug("gstate %d, h %d", (long) gstate, (long) h);

	// calculate akku energy and delta (+)charge (-)discharge when soc between 10-90% and estimate time to live when discharging
	gstate->akku = AKKU_CAPACITY * gstate->soc / 1000;
	int range_ok = gstate->soc > 100 && gstate->soc < 900 && h->soc > 100 && h->soc < 900;
	gstate->dakku = range_ok ? AKKU_CAPACITY_SOC(gstate->soc) - AKKU_CAPACITY_SOC(h->soc) : 0;
	gstate->ttl = gstate->dakku < 0 ? gstate->akku * 60 / gstate->dakku : 60 * 24; // minutes
}

static void calculate_pstate1() {
	// take over raw values from Fronius10
	pstate->akku = r->akku;
	pstate->grid = r->grid;
	pstate->load = r->load;
	pstate->pv10_1 = r->pv10;
	pstate->soc = r->soc * 10.0;

	// clear all flags
	pstate->flags = 0;

	// get 2x history back
	pstate_t *h1 = get_pstate_history(-1);
	pstate_t *h2 = get_pstate_history(-2);

	// offline mode when 3x not enough PV production
	if (pstate->pv10_1 < NOISE && h1->pv10_1 < NOISE && h2->pv10_1 < NOISE) {
		int burnout_time = !SUMMER && (now->tm_hour == 6 || now->tm_hour == 7 || now->tm_hour == 8);
		int burnout_possible = TEMP_IN < 20 && pstate->soc > 150 && gstate->survive > 10;
		if (burnout_time && burnout_possible && AKKU_BURNOUT)
			pstate->flags |= FLAG_BURNOUT; // akku burnout between 6 and 9 o'clock when possible
		else
			pstate->flags |= FLAG_OFFLINE; // offline
		pstate->surplus = pstate->greedy = pstate->modest = pstate->xload = pstate->dxload = pstate->pv7_1 = pstate->pv7_2 = pstate->dpv = 0;
		return;
	}

	// emergency shutdown when three times extreme akku discharge or grid download
	if (pstate->akku > EMERGENCY && h1->akku > EMERGENCY && h2->akku > EMERGENCY && pstate->grid > EMERGENCY && h1->grid > EMERGENCY && h2->grid > EMERGENCY) {
		pstate->flags |= FLAG_EMERGENCY;
		return;
	}

	pstate->flags |= FLAG_RAMP;
}

static void calculate_pstate2() {
	// take over raw values from Fronius7
	pstate->pv7_1 = r->pv7;

	// clear VALID flag
	pstate->flags &= ~FLAG_RAMP;

	// get 2x history back
	pstate_t *h1 = get_pstate_history(-1);
	pstate_t *h2 = get_pstate_history(-2);

	// total pv from both inverters
	pstate->pv = pstate->pv10_1 + pstate->pv10_2 + pstate->pv7_1 + pstate->pv7_2;
	pstate->dpv = pstate->pv - h1->pv;

	// subtract PV produced by Fronius7
	pstate->load -= (pstate->pv7_1 + pstate->pv7_2);

	// calculate delta load
	pstate->dload = pstate->load - h1->load;
	if (abs(pstate->dload) < NOISE)
		pstate->dload = 0;

	// calculate load manually
//	pstate->cload = (pstate->pv + pstate->akku + pstate->grid) * -1;

	// state is stable when we have three times no grid changes
	int deltas = pstate->dgrid + h1->dgrid + h2->dgrid;
	if (!deltas)
		pstate->flags |= FLAG_STABLE;

	// check if we have active devices / all devices in standby
	pstate->flags |= FLAG_ALL_STANDBY;
	for (device_t **dd = DEVICES; *dd != 0; dd++) {
		device_t *d = *dd;
		if (d->state != Standby)
			pstate->flags &= ~FLAG_ALL_STANDBY;
		if (d->power)
			pstate->flags |= FLAG_ACTIVE;
	}

	// distortion when delta pv is too big
	int dpv_sum = 0;
	for (int i = 0; i < PSTATE_HISTORY; i++)
		dpv_sum += abs(pstate_history[i].dpv);
	if (dpv_sum > 1000)
		pstate->flags |= FLAG_DISTORTION;
	// xdebug("FRONIUS distortion=%d sum=%d", PSTATE_DISTORTION, dpv_sum);

	// calculate expected load - use average load between 03 and 04 or default BASELOAD
	pstate->xload = BASELOAD;
	for (device_t **d = DEVICES; *d != 0; d++)
		pstate->xload += (*d)->load;
	pstate->xload *= -1;

	// deviation of calculated load to actual load in %
	pstate->dxload = (pstate->xload - pstate->load) * 100 / pstate->xload;

	// indicate standby check when deviation between actual load and calculated load is three times above 33%
	if (pstate->dxload > 33 && h1->dxload > 33 && h2->dxload > 33)
		pstate->flags |= FLAG_CHECK_STANDBY;

	// surplus is akku charge + grid upload
	pstate->surplus = (pstate->grid + pstate->akku) * -1;

	// greedy power = akku + grid
	// pstate->greedy = (pstate->surplus + h1->surplus + h2->surplus) / 3;
	pstate->greedy = pstate->surplus;
	if (pstate->greedy > 0)
		pstate->greedy -= NOISE; // threshold for ramp up
	if (abs(pstate->greedy) < NOISE)
		pstate->greedy = 0;
	if (!PSTATE_ACTIVE && pstate->greedy < 0)
		pstate->greedy = 0; // no active devices - nothing to ramp down

	// modest power = only grid upload
	// pstate->modest = (pstate->grid + h1->grid + h2->grid) / -3;
	pstate->modest = pstate->grid * -1;
	if (pstate->modest > 0)
		pstate->modest -= NOISE; // threshold for ramp up
	if (abs(pstate->modest) < NOISE)
		pstate->modest = 0;
	if (!PSTATE_ACTIVE && pstate->modest < 0)
		pstate->modest = 0; // no active devices - nothing to ramp down
	if (pstate->greedy < pstate->modest)
		pstate->modest = pstate->greedy; // greedy cannot be smaller than modest

	pstate->flags |= FLAG_RAMP;

	//clear flag if values not valid
	int sum = pstate->grid + pstate->akku + pstate->load + pstate->pv10_1 + pstate->pv10_2;
	if (abs(sum) > SUSPICIOUS) {
		xdebug("FRONIUS suspicious values detected: sum=%d", sum);
		pstate->flags &= ~FLAG_RAMP;
	}
	if (pstate->load > 0) {
		xdebug("FRONIUS positive load detected");
		pstate->flags &= ~FLAG_RAMP;
	}
	if (pstate->grid < -NOISE && pstate->akku > NOISE) {
		int waste = abs(pstate->grid) < pstate->akku ? abs(pstate->grid) : pstate->akku;
		xdebug("FRONIUS wasting %d akku -> grid power", waste);
		pstate->flags &= ~FLAG_RAMP;
	}
	if (pstate->grid - h1->grid > 500) { // e.g. refrigerator starts !!!
		xdebug("FRONIUS grid spike detected %d: %d -> %d", pstate->grid - h1->grid, h1->grid, pstate->grid);
		pstate->flags &= ~FLAG_RAMP;
	}
	if (!potd) {
		xlog("FRONIUS No potd selected!");
		pstate->flags &= ~FLAG_RAMP;
	}
}

static int calculate_next_round(device_t *d) {
	if (d) {
		if (pstate->soc < 1000)
			return WAIT_AKKU; // wait for inverter to adjust charge power
		else
			return WAIT_RAMP;
	}

	if (PSTATE_OFFLINE || PSTATE_BURNOUT)
		return WAIT_OFFLINE;

	// much faster next round on
	// - suspicious values detected
	// - distortion
	// - pv tendence up/down
	// - wasting akku->grid power
	// - big akku discharge or grid download
	// - actual load > calculated load --> other consumers active
	if (!PSTATE_RAMP || PSTATE_DISTORTION || pstate->grid > 500 || pstate->akku > 500 || pstate->dxload < -5)
		return WAIT_NEXT;

	if (PSTATE_ALL_STANDBY)
		return WAIT_STANDBY;

	if (PSTATE_STABLE)
		return WAIT_STABLE;

	return WAIT_INSTABLE;
}

static void emergency() {
	xlog("FRONIUS emergency shutdown at %.1f akku discharge", FLOAT10(pstate->akku));
	set_all_devices(0);
}

static void offline() {
	xlog("FRONIUS offline soc=%.1f temp=%.1f", FLOAT10(pstate->soc), TEMP_IN);
}

// burn out akku between 7 and 9 o'clock if we can re-charge it completely by day
static void burnout() {
	xlog("FRONIUS burnout soc=%.1f temp=%.1f", FLOAT10(pstate->soc), TEMP_IN);
	fronius_override_seconds("plug5", WAIT_OFFLINE);
	fronius_override_seconds("plug6", WAIT_OFFLINE);
	// fronius_override_seconds("plug7", WAIT_OFFLINE); // makes no sense due to ventilate sleeping room
	fronius_override_seconds("plug8", WAIT_OFFLINE);
}

static void monthly(time_t now_ts) {
	xlog("FRONIUS executing monthly tasks...");

	// reset minimum/maximum voltages
	ZEROP(minmax);
	minmax->v1min = minmax->v2min = minmax->v3min = minmax->fmin = 3000;
	minmax->v1max = minmax->v2max = minmax->v3max = minmax->fmax = 0;

	// dump full counter history
	dump_counter(COUNTER_HISTORY);
}

static void daily(time_t now_ts) {
	xlog("FRONIUS executing daily tasks...");

	// bump counter history before writing new values into
	bump_counter(now_ts);

	// store to disk
#ifndef FRONIUS_MAIN
	store_blob_offset(COUNTER_FILE, counter_history, sizeof(*counter), COUNTER_HISTORY, counter_history_ptr);
	store_blob(MINMAX_FILE, minmax, sizeof(minmax_t));
#endif
}

static void hourly(time_t now_ts) {
	xlog("FRONIUS executing hourly tasks...");

	// resetting noresponse counters and standby states
	for (device_t **dd = DEVICES; *dd != 0; dd++) {
		device_t *d = *dd;
		d->noresponse = 0;
		if (d->state == Standby)
			d->state = Active;
	}

	// force all devices off when offline
	if (PSTATE_OFFLINE)
		set_all_devices(0);

	// update raw values
	errors += curl_perform(curl_readable, &memory, &parse_readable);

	// recalculate global state of elapsed hour
	calculate_gstate();

	// dump full gstate history
	dump_gstate();

	// update pointer to current hour slot and take over old values
	gstate_t *gstate_new = &gstate_history[now->tm_hour];
	memcpy(gstate_new, (void*) gstate, sizeof(gstate_t));
	gstate = gstate_new; // atomic update current gstate pointer

	// print actual gstate
	print_gstate(NULL);

	// recalculate mosmix and choose program of the day
	calculate_mosmix(now_ts);
	choose_program();

	// update voltage minimum/maximum
	minimum_maximum(now_ts);
	print_minimum_maximum();
}

static void fronius() {
	int hour, day, mon, wait;
	device_t *device = 0;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// initialize hourly & daily
	time_t now_ts = time(NULL);
	localtime(&now_ts);
	memcpy(now, lt, sizeof(*lt));
	mon = now->tm_mon;
	day = now->tm_wday;
	hour = now->tm_hour;

	// fake yesterday
	// daily(now_ts);

	errors = 0;
	wait = 1;

	// once upon start: calculate global state + discharge rate and choose program of the day
	gstate = &gstate_history[now->tm_hour];
	curl_perform(curl_readable, &memory, &parse_readable);
	calculate_gstate();
	calculate_mosmix(now_ts);
	choose_program();

	// the FRONIUS main loop
	while (1) {

		sleep(1);
		if (wait--)
			continue;

		// get actual calendar time - and make a copy as subsequent calls to localtime() will override them
		now_ts = time(NULL);
		localtime(&now_ts);
		memcpy(now, lt, sizeof(*lt));

		// hourly tasks
		if (hour != now->tm_hour) {
			hour = now->tm_hour;
			hourly(now_ts);
		}

		// daily tasks
		if (day != now->tm_wday) {
			day = now->tm_wday;
			daily(now_ts);
		}

		// monthly tasks
		if (mon != now->tm_mon) {
			mon = now->tm_mon;
			monthly(now_ts);
		}

		// check error counter
		if (errors > 10)
			set_all_devices(0);

		// make Fronius10 API call and calculate first pstate
		errors += curl_perform(curl10, &memory, &parse_fronius10);
		calculate_pstate1();

		// check emergency
		if (PSTATE_EMERGENCY)
			emergency();

		// check offline
		if (PSTATE_OFFLINE)
			offline();

		// check burnout
		if (PSTATE_BURNOUT)
			burnout();

		if (PSTATE_RAMP) {
			// make Fronius7 API call and calculate second pstate
			errors += curl_perform(curl7, &memory, &parse_fronius7);
			calculate_pstate2();
		}

		// print actual pstate
		print_pstate(NULL);

		// prio1: check response from previous action
		if (device)
			device = response(device);

		if (PSTATE_RAMP) {
			// prio2: perform standby check logic
			if (!device)
				device = standby();

			// prio3: check if higher priorized device can steal from lower priorized
			if (!device)
				device = steal();

			// prio4: ramp up/down
			if (!device)
				device = ramp();
		}

		// determine wait for next round
		wait = calculate_next_round(device);

		// print pstate history and device state when we have active devices
		if (PSTATE_ACTIVE) {
			dump_pstate(3);
			print_dstate(wait);
		}

		// set history pointer to next slot
		bump_pstate();
		errors = 0;
	}
}

static int test() {
	response_t memory = { 0 };

	int x[] = { 264, 286, 264, 231, 275, 231, 220, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	int y = average_non_zero(x, ARRAY_SIZE(x));
	xlog("average_non_zero=%d sizeof=%d", y, ARRAY_SIZE(x));
	for (int i = 0; i < 24; i++)
		printf("%d ", x[i]);
	printf("\n");
	printf("sizeof %lu %lu\n", sizeof(x), sizeof(*x));
	ZERO(x);
	for (int i = 0; i < 24; i++)
		printf("%d ", x[i]);
	printf("\n");

	CURL *curl_readable = curl_init(URL_READABLE, &memory);
	if (curl_readable == NULL)
		return xerr("Error initializing libcurl");

	curl_perform(curl_readable, &memory, &parse_readable);
	return 0;

	device_t *d = &b1;

	d->power = -1;
	d->addr = resolve_ip(d->name);

	curl_perform(curl_readable, &memory, &parse_readable);
	printf("curl perform\n");

	return 0;
}

static int init() {
	set_debug(1);

	init_all_devices();
	set_all_devices(0);

	ZEROP(pstate_history);
	ZEROP(gstate_history);
	ZEROP(counter_history);
	ZEROP(r);

	load_blob(GSTATE_FILE, gstate_history, sizeof(gstate_history));
	load_blob(COUNTER_FILE, counter_history, sizeof(counter_history));

	// load or default minimum/maximum
	if (load_blob(MINMAX_FILE, minmax, sizeof(minmax_t))) {
		minmax->v1min = minmax->v2min = minmax->v3min = minmax->fmin = 3000;
		minmax->v1max = minmax->v2max = minmax->v3max = minmax->fmax = 0;
	}

	curl10 = curl_init(URL_FLOW10, &memory);
	if (curl10 == NULL)
		return xerr("Error initializing libcurl");

	curl7 = curl_init(URL_FLOW7, &memory);
	if (curl7 == NULL)
		return xerr("Error initializing libcurl");

	curl_readable = curl_init(URL_READABLE, &memory);
	if (curl_readable == NULL)
		return xerr("Error initializing libcurl");

	// initialize localtime's static structure
	time_t sec = 0;
	lt = localtime(&sec);

	return 0;
}

static void stop() {
#ifndef FRONIUS_MAIN
	store_blob(MINMAX_FILE, minmax, sizeof(minmax_t));
	store_blob(GSTATE_FILE, gstate_history, sizeof(gstate_history));
	store_blob_offset(COUNTER_FILE, counter_history, sizeof(*counter), COUNTER_HISTORY, counter_history_ptr);
#endif

	if (sock)
		close(sock);
}

int fronius_override_seconds(const char *name, int seconds) {
	for (device_t **d = DEVICES; *d != 0; d++) {
		if (!strcmp((*d)->name, name)) {
			xlog("FRONIUS Activating Override on %s", (*d)->name);
			(*d)->override = time(NULL) + seconds;
			(*d)->state = Active;
			((*d)->set_function)((*d), 100);
		}
	}
	return 0;
}

int fronius_override(const char *name) {
	return fronius_override_seconds(name, OVERRIDE);
}

int fronius_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	// no arguments - main loop
	if (argc == 1) {
		init();
		fronius();
		pause();
		stop();
		return 0;
	}

	init_all_devices();

	int c;
	while ((c = getopt(argc, argv, "c:o:t")) != -1) {
		printf("getopt %c\n", c);
		switch (c) {
		case 'o':
			return fronius_override(optarg);
		case 't':
			test();
			printf("test\n");
			return 0;
		}
	}

	return 0;
}

#ifdef FRONIUS_MAIN
int main(int argc, char **argv) {
	return fronius_main(argc, argv);
}
#else
MCP_REGISTER(fronius, 7, &init, &stop, &fronius);
#endif
