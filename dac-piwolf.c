#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include "mcp.h"

void dac_volume_up() {
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEUP");
	mcplog("VOL++");
}

void dac_volume_down() {
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEDOWN");
	mcplog("VOL--");
}

void dac_select_channel() {
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	mcplog("CHANNELUP");
}

int dac_init() {
	return 0;
}

int dac_close() {
	return 0;
}

// WM8741 workaround: switch through all channels
void dac_piwolf_channel() {
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEDOWN");
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEUP");
	mcplog("WM8741 workaround channel");
}

// WM8741 workaround: touch volume
void dac_piwolf_volume() {
	dac_volume_down();
	msleep(100);
	dac_volume_up();
	msleep(100);
	mcplog("WM8741 workaround volume");
}

void dac_on() {
	dac_piwolf_volume();
}

void dac_off() {
}

void dac_update() {
}

void* dac(void *arg) {
	return (void *) 0;
}
