#include <stdlib.h>

#include "mcp.h"
#include "utils.h"

void dac_power() {
}

void dac_volume_up() {
	system("/usr/bin/amixer -q -D hw:CARD=USB2496play set PCM 2%+ >/dev/null");
	xlog("VOL++");
}

void dac_volume_down() {
	system("/usr/bin/amixer -q -D hw:CARD=USB2496play set PCM 2%- >/dev/null");
	xlog("VOL--");
}

void dac_mute() {
}

void dac_unmute() {
}

void dac_source(int source) {
}

int dac_status_get(const void *p1, const void *p2) {
	return 0;
}

void dac_status_set(const void *p1, const void *p2, int value) {
}

int dac_init() {
	return 0;
}

void dac_close() {
}

void dac_handle(int c) {
}
