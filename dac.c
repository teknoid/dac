#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include "mcp.h"

#ifdef WIRINGPI
#include <wiringPi.h>
#endif

void dac_volume_up() {
#ifdef PIWOLF
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEUP");
#endif

#ifdef GPIO_VOL_UP
	digitalWrite(GPIO_VOL_UP, 1);
	msleep(100);
	digitalWrite(GPIO_VOL_UP, 0);
	msleep(100);
#endif

#ifdef ANUS
	system("/usr/bin/amixer set Master 2%+ >/dev/null");
#endif

	mcplog("VOL++");
}

void dac_volume_down() {
#ifdef PIWOLF
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEDOWN");
#endif

#ifdef GPIO_VOL_DOWN
	digitalWrite(GPIO_VOL_DOWN, 1);
	msleep(100);
	digitalWrite(GPIO_VOL_DOWN, 0);
	msleep(100);
#endif

#ifdef ANUS
	system("/usr/bin/amixer set Master 2%- >/dev/null");
#endif

	mcplog("VOL--");
}

void dac_select_channel() {
#ifdef PIWOLF
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
#endif

#ifdef SABRE
#endif

	mcplog("CHANNELUP");
}

int dac_init() {
#ifdef GPIO_VOL_UP
	pinMode(GPIO_VOL_UP, OUTPUT);
	digitalWrite(GPIO_VOL_UP, 0);
#endif
#ifdef GPIO_VOL_DOWN
	pinMode(GPIO_VOL_DOWN, OUTPUT);
	digitalWrite(GPIO_VOL_DOWN, 0);
#endif
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

void* dac(void *arg) {
	return (void *) 0;
}
