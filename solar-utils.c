// gcc -I./include -o solar solar-utils.c utils.c

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "solar-common.h"
#include "utils.h"
#include "mcp.h"

#define GSTATE_H_FILE			"solar-gstate-hours.bin"
#define PSTATE_H_FILE			"solar-pstate-hours.bin"

typedef struct gstate_old_t {
	int pv;
	int pvmin;
	int pvavg;
	int pvmax;
	int produced;
	int consumed;
	int today;
	int tomorrow;
	int sod;
	int eod;
	int soc;
	int ttl;
	int success;
	int forecast;
	int akku;
	int needed;
	int minutes;
	int survive;
	int climit;
	int dlimit;
	int minsoc;
	int flags;
} gstate_old_t;

typedef struct pstate_old_t {
	int pv;
	int grid;
	int akku;
	int ac1;
	int ac2;
	int dc1;
	int dc2;
	int mppt1;
	int mppt2;
	int mppt3;
	int mppt4;
	int p1;
	int p2;
	int p3;
	int v1;
	int v2;
	int v3;
	int f;
	int inv;
	int surp;
	int load;
	int rsl;
	int ramp;
	int flags;
} pstate_old_t;

int akku_get_min_soc() {
	return 0;
}

void akku_set_min_soc(int min) {
}

int akku_charge_max() {
	return 0;
}

int akku_discharge_max() {
	return 0;
}

int akku_standby(device_t *akku) {
	return 0;
}

int akku_charge(device_t *akku, int limit) {
	return 0;
}

int akku_discharge(device_t *akku, int limit) {
	return 0;
}

void inverter_status(int *inv1, int *inv2) {
}

// fake state and counter records from actual values and copy to history records
static int fake() {
	return 0;
}

// update mosmix history from counter history
static int update() {
	return 0;
}

// gstate+pstate structure enhancements: migrate old data to new data
static int migrate() {

	// migrate gstate
	gstate_old_t gold[HISTORY_SIZE];
	gstate_t gnew[HISTORY_SIZE];
	ZERO(gold);
	load_blob(STATE SLASH GSTATE_H_FILE, gold, sizeof(gold));
	for (int i = 0; i < HISTORY_SIZE; i++) {
		gstate_old_t *o = &gold[i];
		gstate_t *n = &gnew[i];
		n->pv = o->pv;
		n->pvmin = o->pvmin;
		n->pvmax = o->pvmax;
		n->pvavg = o->pvavg;
		n->produced = o->produced;
		n->consumed = o->consumed;
		n->today = o->today;
		n->tomorrow = o->tomorrow;
		n->sod = o->sod;
		n->eod = o->eod;
		n->soc = o->soc;
		n->ttl = o->ttl;
		n->success = o->success;
		n->forecast = o->forecast;
		n->akku = o->akku;
		n->needed = o->needed;
		n->minutes = o->minutes
		n->survive = o->survive;
		n->climit = o->climit;
		n->dlimit = o->dlimit;
		n->minsoc = o->minsoc;
		n->flags = o->flags;
	}
	store_blob(TMP SLASH GSTATE_H_FILE, gnew, sizeof(gnew));
	store_blob(STATE SLASH GSTATE_H_FILE, gnew, sizeof(gnew));

	// migrate pstate
	pstate_old_t pold[HISTORY_SIZE];
	pstate_t pnew[HISTORY_SIZE];
	ZERO(pold);
	load_blob(STATE SLASH PSTATE_H_FILE, pold, sizeof(pold));
	for (int i = 0; i < HISTORY_SIZE; i++) {
		pstate_old_t *o = &pold[i];
		pstate_t *n = &pnew[i];
		n->pv = o->pv;
		n->grid = o->grid;
		n->akku = o->akku;
		n->ac1 = o->ac1;
		n->ac2 = o->ac2;
		n->dc1 = o->dc1;
		n->dc2 = o->dc1;
		n->mppt1 = o->mppt1;
		n->mppt2 = o->mppt2;
		n->mppt3 = o->mppt3;
		n->mppt4 = o->mppt4;
		n->p1 = o->p1;
		n->p2 = o->p2;
		n->p3 = o->p3;
		n->v1 = o->v1;
		n->v2 = o->v2;
		n->v3 = o->v3;
		n->f = o->f;
		n->inv = o->inv;
		n->surp = o->surp;
		n->load = o->load; // needed for 24/7 !!!
		n->rsl = o->rsl;
		n->ramp = o->ramp;
		n->flags = o->flags;
	}
	store_blob(TMP SLASH PSTATE_H_FILE, pnew, sizeof(pnew));
	store_blob(STATE SLASH PSTATE_H_FILE, pnew, sizeof(pnew));

	return 0;
}

static int test() {
	int r = 1111;
	int g = 0xffff;
	int b = 32767;
	xlog("TASMOTA r=%d g=%d b=%d", r, g, b);

	r &= 0xff;
	g &= 0xff;
	b &= 0xff;
	xlog("TASMOTA r=%d g=%d b=%d", r, g, b);

	return 0;
}

int main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	int c;
	while ((c = getopt(argc, argv, "fmtu")) != -1) {
		// printf("getopt %c\n", c);
		switch (c) {
		case 'f':
			return fake();
		case 'm':
			return migrate();
		case 't':
			return test();
		case 'u':
			return update();
		default:
			xlog("unknown getopt %c", c);
		}
	}

	return 0;
}
