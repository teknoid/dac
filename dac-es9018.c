#include <linux/input-event-codes.h>
#include <unistd.h>
#include <wiringPi.h>

#include "mcp.h"
#include "utils.h"

#define GPIO_POWER		5

#define GPIO_VOL_UP		4
#define GPIO_VOL_DOWN	0

#define msleep(x) usleep(x*1000)

static void dac_on() {
	digitalWrite(GPIO_POWER, 1);
	mcp->dac_power = 1;
	xlog("switched DAC on");
}

static void dac_off() {
	digitalWrite(GPIO_POWER, 0);
	mcp->dac_power = 0;
	xlog("switched DAC off");
}

void dac_power() {
	if (!mcp->dac_power) {
		dac_on();
		mpdclient_handle(KEY_PLAY);
	} else {
		mpdclient_handle(KEY_STOP);
		dac_off();
	}
}

void dac_volume_up() {
	if (!mcp->dac_power) {
		return;
	}

	digitalWrite(GPIO_VOL_UP, 1);
	msleep(100);
	digitalWrite(GPIO_VOL_UP, 0);
	msleep(100);
	xlog("VOL++");
}

void dac_volume_down() {
	if (!mcp->dac_power) {
		return;
	}

	digitalWrite(GPIO_VOL_DOWN, 1);
	msleep(100);
	digitalWrite(GPIO_VOL_DOWN, 0);
	msleep(100);
	xlog("VOL--");
}

void dac_mute() {
}

void dac_unmute() {
}

void dac_update() {
}

int dac_init() {
	pinMode(GPIO_POWER, OUTPUT);

	pinMode(GPIO_VOL_UP, OUTPUT);
	digitalWrite(GPIO_VOL_UP, 0);
	pinMode(GPIO_VOL_DOWN, OUTPUT);
	digitalWrite(GPIO_VOL_DOWN, 0);

	mcp->dac_power = digitalRead(GPIO_POWER);
	if (mcp->dac_power) {
		xlog("DAC is powered on");
	} else {
		xlog("DAC is powered off");
	}

	return 0;
}

void dac_close() {
}

void dac_handle(int c) {
	switch (c) {
	case KEY_VOLUMEUP:
		dac_volume_up();
		break;
	case KEY_VOLUMEDOWN:
		dac_volume_down();
		break;
	case KEY_POWER:
		dac_power();
		break;
	default:
		mpdclient_handle(c);
	}
}
