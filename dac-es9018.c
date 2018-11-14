#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>

#include <wiringPi.h>

#include <linux/input.h>

#include "mcp.h"

#define GPIO_POWER		5

#define GPIO_VOL_UP		4
#define GPIO_VOL_DOWN	0

static void external(char *key) {
	char command[64];
	strcpy(command, EXTERNAL);
	strcat(command, " ");
	strcat(command, key);
	system(command);
}

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
		mcplog("entered power state ON");
	} else {
		mcp->power = stdby;
		mcplog("entered power state STDBY");
	}

	return 0;
}

void dac_close() {
}

void dac_handle(int key) {
	switch (key) {
	case KEY_EJECTCD:
		mpdclient_set_playlist_mode(0);
		external("RANDOM");
		break;
	case KEY_F1:
		external("F1");
		break;
	case KEY_F2:
		external("F2");
		break;
	case KEY_F3:
		external("F3");
		break;
	case KEY_F4:
		external("F4");
		break;
	}
}

void *dac(void *arg) {
	return (void *) 0;
}
