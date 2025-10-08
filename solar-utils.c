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

typedef struct gstate_old_t {
	int pv;
	int pvmin;
	int pvmax;
	int pvavg;
	int produced;
	int consumed;
	int today;
	int tomorrow;
	int sod;
	int eod;
	int load;
	int baseload;
	int soc;
	int akku;
	int ttl;
	int success;
	int forecast;
	int survive;
	int nsurvive;
	int flags;
} gstate_old_t;

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

// gstate structure enhancements: migrate old data to new data
static int migrate() {
	gstate_old_t old[HISTORY_SIZE];
	gstate_t new[HISTORY_SIZE];

	ZERO(old);
	load_blob(STATE SLASH GSTATE_H_FILE, old, sizeof(old));
	for (int i = 0; i < HISTORY_SIZE; i++) {
		gstate_old_t *o = &old[i];
		gstate_t *n = &new[i];

		n->pv = o->pv;
		n->produced = o->produced;
		n->consumed = o->consumed;
		n->today = o->today;
		n->tomorrow = o->tomorrow;
		n->sod = o->sod;
		n->eod = o->eod;
		n->load = o->load;
		n->soc = o->soc;
		n->akku = o->akku;
		n->ttl = o->ttl;
		n->success = o->success;
		n->survive = o->survive;
	}

	// test and verify
	store_blob(TMP SLASH GSTATE_H_FILE, new, sizeof(new));
	// live
	store_blob(STATE SLASH GSTATE_H_FILE, new, sizeof(new));
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
