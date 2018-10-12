#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <wiringPi.h>

#include "mcp.h"

#define GPIO_POWER		0

void dac_volume_up() {
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEUP");
	mcplog("VOL++");
}

void dac_volume_down() {
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEDOWN");
	mcplog("VOL--");
}

void dac_mute() {
	mcplog("MUTE");
}

void dac_unmute() {
	mcplog("UNMUTE");
}

void dac_select_channel() {
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	mcplog("CHANNELUP");
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
	digitalWrite(GPIO_POWER, 1);
	mcplog("switched POWER on");
	sleep(3);
	dac_piwolf_volume();
}

void dac_off() {
	digitalWrite(GPIO_POWER, 0);
	mcplog("switched POWER off");

}

void dac_update() {
}

int dac_init() {
	pinMode(GPIO_POWER, OUTPUT);

	int pin = digitalRead(GPIO_POWER);
	if (pin == 1) {
		power_state = on;
		mcplog("entered power state ON");
	} else {
		power_state = stdby;
		mcplog("entered power state STDBY");
	}

	return 0;
}

void dac_close() {
}

void *dac(void *arg) {
	return (void *) 0;
}
