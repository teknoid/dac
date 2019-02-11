#include <linux/input-event-codes.h>

#ifdef WIRINGPI
#include <wiringPi.h>
#endif

#include "mcp.h"
#include "utils.h"

int current_value;

static void dac_on() {
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 1);
#endif
	mcp->dac_power = 1;
	xlog("switched DAC on");
}

static void dac_off() {
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 0);
#endif
	xlog("switched DAC off");
	mcp->dac_power = 0;
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
	xlog("VOL++");
}

void dac_volume_down() {
	xlog("VOL--");
}

void dac_mute() {
	mcp->dac_mute = 1;
	xlog("MUTE");
}

void dac_unmute() {
	mcp->dac_mute = 0;
	xlog("UNMUTE");
}

void dac_source(int source) {
	xlog("SOURCE");
}

int dac_config_get(const void *ptr) {
	xlog("dac_config_get");
	return current_value;
}

void dac_config_set(const void *ptr, int value) {
	xlog("dac_config_set");
	current_value = value;
}

int dac_init() {
#ifdef GPIO_POWER
	pinMode(GPIO_POWER, OUTPUT);
	mcp->dac_power = digitalRead(GPIO_POWER);
	if (mcp->dac_power) {
		xlog("DAC  power is ON");
	} else {
		xlog("DAC  power is OFF");
	}

#endif
	return 0;
}

void dac_close() {
}

void dac_handle(int c) {
	mpdclient_handle(c);
}
