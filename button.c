#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <linux/input-event-codes.h>

#include "utils.h"
#include "i2c.h"
#include "mcp.h"

#define ADDR	0x20

static int i2c;
static pthread_t thread;
static void* button(void *arg);

int button_init() {
	if ((i2c = open(I2C, O_RDWR)) < 0)
		return xerr("I2C BUS error");

	if (pthread_create(&thread, NULL, &button, NULL))
		return xerr("Error creating button thread");

	xlog("BUTTON initialized");
	return 0;
}

void button_close() {
	if (pthread_cancel(thread))
		xlog("Error canceling thread_rb");

	if (pthread_join(thread, NULL))
		xlog("Error joining thread_rb");

	if (i2c > 0)
		close(i2c);
}

void handle_button(unsigned char c) {
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
	}
}

static void* button(void *arg) {
	unsigned char c, c_old, hold;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	c_old = i2c_get(i2c, ADDR);

	while (1) {
		msleep(100);
		c = i2c_get(i2c, ADDR);

		if (c == 0xff)
			hold = 0;
		else if (c == c_old)
			hold++;

		if (hold > 5 || (c != 0xff && c != c_old))
			handle_button(0xff - c);

		c_old = c;
	}
}
