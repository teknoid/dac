#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <linux/input.h>

#include "mcp.h"

void poweron() {
	dac_on();
	mpdclient_handle(KEY_PLAY);
	power_state = on;
	mcplog("entered power state ON");
}

void poweroff() {
	dac_off();
	power_state = off;
	mcplog("entered power state OFF");
	system("shutdown -h now");
}

void standby() {
	dac_off();
	mpdclient_handle(KEY_STOP);
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
	return 0;
}
