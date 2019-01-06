#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <linux/input.h>

#include "mcp.h"

void poweron() {
	dac_on();
	mcp->power = on;
	mpdclient_handle(KEY_PLAY);
	mcplog("entered power state ON");
}

void poweroff() {
	dac_off();
	mcp->power = off;
	mcplog("entered power state OFF");
	system("shutdown -h now");
}

void standby() {
	dac_off();
	mcp->power = stdby;
	mpdclient_handle(KEY_STOP);
	mcplog("entered power state STDBY");
}

void power_soft() {
	if (mcp->power == startup || mcp->power == stdby) {
		poweron();
	} else if (mcp->power == on) {
		standby();
	}
}

void power_hard() {
	poweroff();
}

int power_init() {
	mcp->power = startup;
	mcplog("entered power state STARTUP");
	return 0;
}
