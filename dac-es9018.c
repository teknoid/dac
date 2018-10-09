#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <wiringPi.h>

#include "mcp.h"

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

void dac_select_channel() {
	mcplog("CHANNELUP");
}

int dac_init() {
	pinMode(GPIO_VOL_UP, OUTPUT);
	digitalWrite(GPIO_VOL_UP, 0);
	pinMode(GPIO_VOL_DOWN, OUTPUT);
	digitalWrite(GPIO_VOL_DOWN, 0);
	return 0;
}

int dac_close() {
	return 0;
}

void dac_on() {
}

void dac_off() {
}

void dac_update() {
}

void* dac(void *arg) {
	return (void *) 0;
}
