#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef WIRINGPI
#include <wiringPi.h>
#endif

#include "mcp.h"
#include "utils.h"

static void dac_on() {
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 1);
	xlog("switched DAC on");
#endif
}

static void dac_off() {
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 0);
	xlog("switched DAC off");
#endif
}

void dac_power() {
	if (mcp->power == startup || mcp->power == stdby) {
		dac_on();
		mcp->power = on;
		mpdclient_handle(KEY_PLAY);
		xlog("entered power state ON");
	} else if (mcp->power == on) {
		dac_off();
		mcp->power = stdby;
		mpdclient_handle(KEY_STOP);
		xlog("entered power state STDBY");
	}
}

void dac_volume_up() {
	xlog("VOL++");
}

void dac_volume_down() {
	xlog("VOL--");
}

void dac_mute() {
	xlog("MUTE");
}

void dac_unmute() {
	xlog("UNMUTE");
}

void dac_update() {
}

int dac_init() {
#ifdef GPIO_POWER
	pinMode(GPIO_POWER, OUTPUT);
	int pin = digitalRead(GPIO_POWER);
	if (pin == 1) {
		power_state = on;
		xlog("entered power state ON");
	} else {
		power_state = stdby;
		xlog("entered power state STDBY");
	}
#endif
	return 0;
}

void dac_close() {
}

void dac_handle(struct input_event ev) {
	mpdclient_handle(ev.code);
}
