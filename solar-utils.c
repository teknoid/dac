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
	int mppt1p;
	int mppt2p;
	int mppt3p;
	int mppt4p;
	int mppt1v;
	int mppt2v;
	int mppt3v;
	int mppt4v;
	int l1p;
	int l2p;
	int l3p;
	int l1v;
	int l2v;
	int l3v;
	int f;
	int surp;
	int load;
	int rsl;
	int ramp;
	int flags;
} pstate_old_t;

void inverter_disconnect() {
}

void inverter_connect() {
}

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

int akku_charge(device_t *akku) {
	return 0;
}

int akku_discharge(device_t *akku) {
	return 0;
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
//		n->loadmin = o->loadmin;
//		n->loadavg = o->loadavg;
//		n->loadmax = o->loadmax;
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
		n->minutes = o->minutes;
		n->survive = o->survive;
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
		n->mppt1p = o->mppt1p;
		n->mppt2p = o->mppt2p;
		n->mppt3p = o->mppt3p;
		n->mppt4p = o->mppt4p;
		n->l1p = o->l1p;
		n->l2p = o->l2p;
		n->l3p = o->l3p;
		n->l1v = o->l1v;
		n->l2v = o->l2v;
		n->l3v = o->l3v;
		n->f = o->f;
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
