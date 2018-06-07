#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <linux/input.h>

#include "mcp.h"

#ifdef WIRINGPI
#include <wiringPi.h>
#endif

typedef enum {
	startup, stdby, on, off
} state_t;

state_t state;

unsigned long tstart = 0;

void poweron() {
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 1);
#ifdef PIWOLF
	dac_piwolf_volume();
#endif
	sleep(3);
#endif
	mpdclient_handle(KEY_PLAY);
	state = on;
	mcplog("entered state ON");
}

void poweroff() {
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 0);
#endif
	system("shutdown -h now");
	state = off;
	mcplog("entered state OFF");
}

void standby() {
	mpdclient_handle(KEY_STOP);
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 0);
#endif
	state = stdby;
	mcplog("entered state STDBY");
}

void power_soft() {
	if (state == startup || state == stdby) {
		poweron();
	} else if (state == on) {
		standby();
	}
}

void power_hard() {
	poweroff();
}

int power_init() {
	state = startup;
	mcplog("entered state STARTUP");

#ifdef GPIO_POWER
	int pinState = digitalRead(GPIO_POWER);
	if (pinState == 1) {
		state = on;
		mcplog("entered state ON");
	} else {
		state = stdby;
		mcplog("entered state STDBY");
	}

	pinMode(GPIO_POWER, OUTPUT);
#endif

	return 0;
}
