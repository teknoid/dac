#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include <linux/input.h>

#include <wiringPi.h>

#include "mcp.h"
#include "utils.h"

#define GPIO_POWER		5

#define GPIO_VOL_UP		4
#define GPIO_VOL_DOWN	0

void dac_volume_up() {
	digitalWrite(GPIO_VOL_UP, 1);
	msleep(100);
	digitalWrite(GPIO_VOL_UP, 0);
	msleep(100);
	xlog("VOL++");
}

void dac_volume_down() {
	digitalWrite(GPIO_VOL_DOWN, 1);
	msleep(100);
	digitalWrite(GPIO_VOL_DOWN, 0);
	msleep(100);
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
}

void dac_off() {
	digitalWrite(GPIO_POWER, 0);
}

void dac_update() {
}

int dac_init() {
	pinMode(GPIO_POWER, OUTPUT);

	pinMode(GPIO_VOL_UP, OUTPUT);
	digitalWrite(GPIO_VOL_UP, 0);
	pinMode(GPIO_VOL_DOWN, OUTPUT);
	digitalWrite(GPIO_VOL_DOWN, 0);

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
	case KEY_VOLUMEUP:
		dac_volume_up();
		break;
	case KEY_VOLUMEDOWN:
		dac_volume_down();
		break;
	case KEY_POWER:
		power_soft();
		break;
	default:
		mpdclient_handle(ev.code);
	}
}
