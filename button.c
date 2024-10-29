/*****************************************************************************

 read button press/release events from a PCF8574X Port expander connected
 via I2C over CH341 USB Bridge Controller

 Using kernel driver from Allan Bian
 https://github.com/allanbian1017/i2c-ch341-usb

 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "tasmota.h"
#include "fronius.h"
#include "button.h"
#include "utils.h"
#include "dac.h"
#include "i2c.h"
#include "mcp.h"
#include "mpd.h"

#ifndef I2C
#define I2C				"/dev/i2c-3"
#endif

static int i2cfd;

static void handle_button(unsigned char c) {
	if (c == 128)
		return; // this is the shift button

	xlog("BUTTON handle %d", c);

	switch (c) {
	case 1:
		mpdclient_handle(KEY_PAUSE);
		break;
	case 2:
		dac_volume_down();
		break;
	case 4:
		dac_volume_up();
		break;
	case 8:
		mpdclient_handle(KEY_NEXTSONG);
		break;
	case 16:
		system("/m/party.sh");
		break;
	case 32:
#ifdef FRONIUS
		fronius_override_seconds("plug4", 3600);
#endif
		break;
	case 160:
#ifdef FRONIUS
		fronius_override_seconds("plug6", 3600);
#endif
		break;
	}
}

static void button() {
	unsigned char c, c_old, hold;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	i2c_get(i2cfd, ADDR, &c_old);

	while (1) {
		msleep(100);
		i2c_get(i2cfd, ADDR, &c);

		if (c == 0xff)
			hold = 0;
		else if (c == c_old)
			hold++;

		if (hold > 5 || (c != 0xff && c != c_old))
			handle_button(0xff - c);

		c_old = c;
	}
}

static int init() {
	if ((i2cfd = open(I2C, O_RDWR)) < 0)
		return xerr("error opening  %s", I2C);

	return 0;
}

static void stop() {
	if (i2cfd > 0)
		close(i2cfd);
}

MCP_REGISTER(button, 5, &init, &stop, &button);
