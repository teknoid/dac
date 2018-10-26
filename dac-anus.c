#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "mcp.h"

void dac_volume_up() {
	system("/usr/bin/amixer set Master 2%+ >/dev/null");
	mcplog("VOL++");
}

void dac_volume_down() {
	system("/usr/bin/amixer set Master 2%- >/dev/null");
	mcplog("VOL--");
}

void dac_mute() {
	mcplog("MUTE");
}

void dac_unmute() {
	mcplog("UNMUTE");
}

void dac_on() {
}

void dac_off() {
}

void dac_update() {
}

int dac_init() {
	return 0;
}

void dac_close() {
}

void dac_handle(int key) {
}

void *dac(void *arg) {
	return (void *) 0;
}
