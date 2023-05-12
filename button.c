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
#include <pthread.h>

#include <linux/input-event-codes.h>

#include "button.h"
#include "utils.h"
#include "dac.h"
#include "i2c.h"
#include "mpd.h"
#include "mcp.h"

#ifndef I2C
#define I2C				"/dev/i2c-3"
#endif

static int i2cfd;
static pthread_t thread;

static void handle_button(unsigned char c) {
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
	}
}

static void* button(void *arg) {
	unsigned char c, c_old, hold;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
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

	if (pthread_create(&thread, NULL, &button, NULL))
		return xerr("Error creating button thread");

	xlog("BUTTON initialized");
	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("Error canceling thread_rb");

	if (pthread_join(thread, NULL))
		xlog("Error joining thread_rb");

	if (i2cfd > 0)
		close(i2cfd);
}

MCP_REGISTER(button, 5, &init, &stop);
