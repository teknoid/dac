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

unsigned long tstart = 0;

void poweron() {
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 1);
	sleep(3);
#endif
	dac_on();
	mpdclient_handle(KEY_PLAY);
	power_state = on;
	mcplog("entered power state ON");
}

void poweroff() {
	dac_off();
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 0);
#endif
	system("shutdown -h now");
	power_state = off;
	mcplog("entered power state OFF");
}

void standby() {
	dac_off();
	mpdclient_handle(KEY_STOP);
#ifdef GPIO_POWER
	digitalWrite(GPIO_POWER, 0);
#endif
	power_state = stdby;
	mcplog("entered power state STDBY");
}

void power_soft() {
	if (power_state == startup || power_state == stdby) {
		poweron();
	} else if (power_state == on) {
		standby();
	}
}

void power_hard() {
	poweroff();
}

int power_init() {
	power_state = startup;
	mcplog("entered power state STARTUP");

#ifdef GPIO_POWER
	int pinpower_state = digitalRead(GPIO_POWER);
	if (pinpower_state == 1) {
		power_state = on;
		mcplog("entered power state ON");
	} else {
		power_state = stdby;
		mcplog("entered power state STDBY");
	}

	pinMode(GPIO_POWER, OUTPUT);
#endif

	return 0;
}
