#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "mcp.h"
#include "utils.h"

void dac_volume_up() {
	system("/usr/bin/amixer set Master 2%+ >/dev/null");
	xlog("VOL++");
}

void dac_volume_down() {
	system("/usr/bin/amixer set Master 2%- >/dev/null");
	xlog("VOL--");
}

void dac_mute() {
	xlog("MUTE");
}

void dac_unmute() {
	xlog("UNMUTE");
}

void dac_on() {
}

void dac_off() {
}

void dac_update() {
}

int dac_init() {
	mcp->power = on;
	return 0;
}

void dac_close() {
}

void dac_handle(struct input_event ev) {
}
