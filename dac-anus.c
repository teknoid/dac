#include <stdlib.h>

#include "mcp.h"
#include "utils.h"

void dac_power() {
}

void dac_volume_up() {
	system("/usr/bin/amixer set Master 2%+ >/dev/null");
	xlog("VOL++");
}

void dac_volume_down() {
	system("/usr/bin/amixer set Master 2%- >/dev/null");
	xlog("VOL--");
}

void dac_mute() {
}

void dac_unmute() {
}

void dac_source(dac_source_t source) {
}

int dac_init() {
	return 0;
}

void dac_close() {
}

void dac_handle(int c) {
}
