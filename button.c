#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <linux/input-event-codes.h>

#include "utils.h"
#include "mcp.h"

#define ADDR	0x20

static int i2cfd;
static pthread_t thread;
static void* button(void *arg);

int button_init() {
	// TODO config
	i2cfd = open(I2C, O_RDWR);
	if (i2cfd < 0)
		xlog("I2C BUS error");

	if (ioctl(i2cfd, I2C_SLAVE, ADDR) < 0) {
		xlog("Error addressing device 0x%02x", ADDR);
		return -1;
	}

	if (pthread_create(&thread, NULL, &button, NULL)) {
		xlog("Error creating button thread");
		return -1;
	}

	xlog("BUTTON initialized");
	return 0;
}

void button_close() {
	if (pthread_cancel(thread)) {
		xlog("Error canceling thread_rb");
	}
	if (pthread_join(thread, NULL)) {
		xlog("Error joining thread_rb");
	}

	if (i2cfd > 0)
		close(i2cfd);
}

void handle_button(unsigned char c) {
	xlog("handle button %d", c);

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

	if (read(i2cfd, &c_old, 1) != 1)
		xlog("Error reading from device");

	while (1) {
		msleep(100);

		if (read(i2cfd, &c, 1) != 1)
			xlog("Error reading from device");

		if (c == 0xff)
			hold = 0;
		else if (c == c_old)
			hold++;

		if (hold > 5 || (c != 0xff && c != c_old))
			handle_button(0xff - c);

		c_old = c;
	}
}
