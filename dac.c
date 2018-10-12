#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef WIRINGPI
#include <wiringPi.h>
#endif

#include "mcp.h"

void dac_volume_up() {
	mcplog("VOL++");
}

void dac_volume_down() {
	mcplog("VOL--");
}

void dac_select_channel() {
	mcplog("CHANNELUP");
}

void dac_mute() {
	mcplog("MUTE");
}

void dac_unmute() {
	mcplog("UNMUTE");
}

void dac_on() {
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 1);
	mcplog("switched DAC on");
#endif
}

void dac_off() {
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 0);
	mcplog("switched DAC off");
#endif
}

void dac_update() {
}

int dac_init() {
#ifdef GPIO_POWER
	pinMode(GPIO_POWER, OUTPUT);
	int pin = digitalRead(GPIO_POWER);
	if (pin == 1) {
		power_state = on;
		mcplog("entered power state ON");
	} else {
		power_state = stdby;
		mcplog("entered power state STDBY");
	}
#endif

	return 0;
}

void dac_close() {
}

void *dac(void *arg) {
	return (void *) 0;
}
