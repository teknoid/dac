#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <linux/input.h>

#include <wiringPi.h>

#include "mcp.h"
#include "utils.h"

#define GPIO_POWER		0

// WM8741 workaround: switch through all channels
static void workaround_channel() {
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEDOWN");
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEUP");
	xlog("WM8741 workaround channel");
}

// WM8741 workaround: touch volume
static void workaround_volume() {
	dac_volume_down();
	msleep(100);
	dac_volume_up();
	msleep(100);
	xlog("WM8741 workaround volume");
}

void dac_volume_up() {
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEUP");
	xlog("VOL++");
}

void dac_volume_down() {
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEDOWN");
	xlog("VOL--");
}

void dac_mute() {
	xlog("MUTE");
}

void dac_unmute() {
	xlog("UNMUTE");
}

void dac_on() {
	digitalWrite(GPIO_POWER, 1);
	xlog("switched POWER on");
	sleep(3);
	workaround_volume();
}

void dac_off() {
	digitalWrite(GPIO_POWER, 0);
	xlog("switched POWER off");

}

void dac_update() {
}

int dac_init() {
	pinMode(GPIO_POWER, OUTPUT);

	int pin = digitalRead(GPIO_POWER);
	if (pin == 1) {
		mcp->power = on;
		xlog("entered power state ON");
	} else {
		mcp->power = stdby;
		xlog("entered power state STDBY");
	}

	return 0;
}

void dac_close() {
}

void dac_handle(struct input_event ev) {
	switch (ev.code) {
	case KEY_PAUSE:
	case KEY_PLAY:
		workaround_volume();
		mpdclient_handle(ev.code);
		break;
	case KEY_EJECTCD:
		workaround_channel();
		break;
	case KEY_SELECT:
		lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
		xlog("CHANNELUP");
		break;
	default:
		mpdclient_handle(ev.code);
	}
}
