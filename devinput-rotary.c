#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#include <linux/input.h>
#include <linux/input-event-codes.h>

#include "dac.h"
#include "mcp.h"
#include "utils.h"

#ifndef DEVINPUT_RA
#define DEVINPUT_RA			"/dev/input/rotary_axis"
#endif

#ifndef DEVINPUT_RB
#define DEVINPUT_RB			"/dev/input/rotary_button"
#endif

// #define LOCALMAIN

static int fd_ra;
static pthread_t thread_ra;

static int fd_rb;
static pthread_t thread_rb;

static void* rotary_axis(void *arg) {
	struct input_event ev;
	int n;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		n = read(fd_ra, &ev, sizeof(ev));
		if (n == -1) {
			if (errno == EINTR)
				return (void*) 0;
			else
				break;
		} else if (n != sizeof(ev)) {
			errno = EIO;
			break;
		}
		// xlog("axis type 0x%0x code 0x%0x value 0x%0x", ev.type, ev.code, ev.value);

		if (ev.type != EV_REL)
			continue; // ignore others

		xlog("ROTARY: distributing axis %s (0x%0x)", devinput_keyname(ev.code), ev.code);
		switch (ev.value) {
		case -1:
#ifndef LOCALMAIN
			dac_handle(KEY_VOLUMEDOWN);
#endif
			break;
		case +1:
#ifndef LOCALMAIN
			dac_handle(KEY_VOLUMEUP);
#endif
			break;
		}
	}

	xlog("ROTARY axis error %s", strerror(errno));
	return (void*) 0;
}

static void* rotary_button(void *arg) {
	struct input_event ev;
	int n;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		n = read(fd_rb, &ev, sizeof ev);
		if (n == -1) {
			if (errno == EINTR)
				return (void*) 0;
			else
				break;
		} else if (n != sizeof ev) {
			errno = EIO;
			break;
		}
		// xlog("button type 0x%0x code 0x%0x value 0x%0x", ev.type, ev.code, ev.value);

		if (ev.type != EV_KEY)
			continue; // ignore others

		xlog("ROTARY: distributing button %s (0x%0x) %d", devinput_keyname(ev.code), ev.code, ev.value);
		if (ev.value == 1) {
#ifndef LOCALMAIN
			dac_handle(ev.code); // only KEYDOWN event
#endif
		}
	}

	xlog("ROTARY button error %s", strerror(errno));
	return (void*) 0;
}

static int init() {
	char name[256] = "";

	// Open Devices
	if ((fd_ra = open(DEVINPUT_RA, O_RDONLY)) == -1)
		return xerr("unable to open %s", DEVINPUT_RA);

	if ((fd_rb = open(DEVINPUT_RB, O_RDONLY)) == -1)
		return xerr("unable to open %s", DEVINPUT_RB);

	// Print Device Name
	ioctl(fd_ra, EVIOCGNAME(sizeof(name)), name);
	xlog("ROTARY AXIS: reading from %s (%s)", DEVINPUT_RA, name);
	ioctl(fd_rb, EVIOCGNAME(sizeof(name)), name);
	xlog("ROTARY BUTTON: reading from %s (%s)", DEVINPUT_RB, name);

	// start listener
	if (pthread_create(&thread_ra, NULL, &rotary_axis, NULL))
		return xerr("Error creating thread_ra");

	if (pthread_create(&thread_rb, NULL, &rotary_button, NULL))
		return xerr("Error creating thread_rb");

	xlog("ROTARY initialized");
	return 0;
}

static void stop() {
	if (pthread_cancel(thread_ra))
		xlog("Error canceling thread_ra");

	if (pthread_join(thread_ra, NULL))
		xlog("Error joining thread_ra");

	if (fd_ra)
		close(fd_ra);

	if (pthread_cancel(thread_rb))
		xlog("Error canceling thread_rb");

	if (pthread_join(thread_rb, NULL))
		xlog("Error joining thread_rb");

	if (fd_rb)
		close(fd_rb);
}

MCP_REGISTER(rotary, 5, &init, &stop);

#ifdef LOCALMAIN

int main(void) {
	init();
	int c = getchar();
	stop();
}

#endif
