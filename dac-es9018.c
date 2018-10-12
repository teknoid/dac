#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <wiringPi.h>

#include "mcp.h"

#define GPIO_POWER		5

#define GPIO_VOL_UP		4
#define GPIO_VOL_DOWN	0

void dac_volume_up() {
	digitalWrite(GPIO_VOL_UP, 1);
	msleep(100);
	digitalWrite(GPIO_VOL_UP, 0);
	msleep(100);
	mcplog("VOL++");
}

void dac_volume_down() {
	digitalWrite(GPIO_VOL_DOWN, 1);
	msleep(100);
	digitalWrite(GPIO_VOL_DOWN, 0);
	msleep(100);
	mcplog("VOL--");
}

void dac_mute() {
	mcplog("MUTE");
}

void dac_unmute() {
	mcplog("UNMUTE");
}

void dac_select_channel() {
	mcplog("CHANNELUP");
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
