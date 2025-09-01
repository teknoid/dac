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

#include "ledstrip.h"
#include "tasmota.h"
#include "button.h"
#include "utils.h"
#include "dac.h"
#include "i2c.h"
#include "mcp.h"
#include "mpd.h"

#ifdef SOLAR
#include "solar.h"
#endif

#ifndef I2C
#define I2C				"/dev/i2c-3"
#endif

#define SHIFT			128

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
#ifdef SOLAR
	case 32:
		solar_override_seconds("tisch", 3600);
		break;
	case 32 + SHIFT:
		solar_override_seconds("wozi", 3600);
		break;
#endif
	case 64:
		ledstrip_toggle();
		break;
	case 64 + SHIFT:
		ledstrip_blink_red(3);
		break;
	default:
	}
}

static void button() {
	unsigned char c, c_old, hold = 0;

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
