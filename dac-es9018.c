#include <linux/input-event-codes.h>
#include <unistd.h>
#include <wiringPi.h>

#include "utils.h"
#include "dac-es9018.h"

#define msleep(x) usleep(x*1000)

static void gpio_toggle(int gpio) {
	int state = digitalRead(gpio);
	if (state == 0) {
		digitalWrite(gpio, 1); // on
		return;
	}
	if (state == 1) {
		digitalWrite(gpio, 0); // off
		return;
	}
}

static void dac_on() {
	digitalWrite(GPIO_DAC_POWER, 1);
	mcp->dac_power = 1;
	xlog("switched DAC on");

	msleep(500);

	digitalWrite(GPIO_EXT_POWER, 1);
	mcp->ext_power = 1;
	xlog("switched EXT on");
}

static void dac_off() {
	digitalWrite(GPIO_EXT_POWER, 0);
	mcp->ext_power = 0;
	xlog("switched EXT off");

	msleep(500);

	digitalWrite(GPIO_DAC_POWER, 0);
	mcp->dac_power = 0;
	xlog("switched DAC off");
}

void dac_power() {
	if (!mcp->dac_power) {
		dac_on();
		// wait for XMOS USB boot
		msleep(5000);
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

void dac_source(int source) {
}

int dac_status_get(const void *p1, const void *p2) {
	return 0;
}

void dac_status_set(const void *p1, const void *p2, int value) {
}

int dac_init() {
	pinMode(GPIO_DAC_POWER, OUTPUT);
	pinMode(GPIO_EXT_POWER, OUTPUT);
	pinMode(GPIO_LAMP, OUTPUT);

	pinMode(GPIO_VOL_UP, OUTPUT);
	digitalWrite(GPIO_VOL_UP, 0);
	pinMode(GPIO_VOL_DOWN, OUTPUT);
	digitalWrite(GPIO_VOL_DOWN, 0);

	mcp->dac_power = digitalRead(GPIO_DAC_POWER);
	xlog("DAC power is %s", mcp->dac_power ? "ON" : "OFF");
	mcp->ext_power = digitalRead(GPIO_EXT_POWER);
	xlog("EXT power is %s", mcp->ext_power ? "ON" : "OFF");

	xlog("ES9018 initialized");
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
	case KEY_REDO:
	case KEY_TIME:
		gpio_toggle(GPIO_LAMP);
		break;
	default:
		mpdclient_handle(c);
	}
}
