#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <wiringPi.h>

#include <linux/input.h>

#include "mcp.h"

#define GPIO_POWER		0

// WM8741 workaround: switch through all channels
static void workaround_channel() {
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEDOWN");
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEUP");
	mcplog("WM8741 workaround channel");
}

// WM8741 workaround: touch volume
static void workaround_volume() {
	dac_volume_down();
	msleep(100);
	dac_volume_up();
	msleep(100);
	mcplog("WM8741 workaround volume");
}

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

void dac_on() {
	digitalWrite(GPIO_POWER, 1);
	mcplog("switched POWER on");
	sleep(3);
	workaround_volume();
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

void dac_handle(int key) {
	switch (key) {
	case KEY_PAUSE:
	case KEY_PLAY:
		workaround_volume();
		break;
	case KEY_EJECTCD:
		workaround_channel();
		break;
	case KEY_SELECT:
		lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
		mcplog("CHANNELUP");
		break;
	}
}

void *dac(void *arg) {
	return (void *) 0;
}
