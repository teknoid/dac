#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/input.h>

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
static void *rotary_axis(void *arg);

static int fd_rb;
static pthread_t thread_rb;
static void *rotary_button(void *arg);

int rotary_init() {
	char name[256] = "";

	// Open Devices
	if ((fd_ra = open(DEVINPUT_RA, O_RDONLY)) == -1) {
		xlog("unable to open %s", DEVINPUT_RA);
	}
	if ((fd_rb = open(DEVINPUT_RB, O_RDONLY)) == -1) {
		xlog("unable to open %s", DEVINPUT_RB);
	}

	// Print Device Name
	ioctl(fd_ra, EVIOCGNAME(sizeof(name)), name);
	xlog("ROTARY AXIS: reading from %s (%s)", DEVINPUT_RA, name);
	ioctl(fd_rb, EVIOCGNAME(sizeof(name)), name);
	xlog("ROTARY BUTTON: reading from %s (%s)", DEVINPUT_RB, name);

	// start listener
	if (pthread_create(&thread_ra, NULL, &rotary_axis, NULL)) {
		xlog("Error creating thread_ra");
	}
	if (pthread_create(&thread_rb, NULL, &rotary_button, NULL)) {
		xlog("Error creating thread_rb");
	}

	return 0;
}

void rotary_close() {
	if (pthread_cancel(thread_ra)) {
		xlog("Error canceling thread_ra");
	}
	if (pthread_join(thread_ra, NULL)) {
		xlog("Error joining thread_ra");
	}
	if (fd_ra) {
		close(fd_ra);
	}

	if (pthread_cancel(thread_rb)) {
		xlog("Error canceling thread_rb");
	}
	if (pthread_join(thread_rb, NULL)) {
		xlog("Error joining thread_rb");
	}
	if (fd_rb) {
		close(fd_rb);
	}
}

static void *rotary_axis(void *arg) {
	struct input_event ev;
	int n;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	while (1) {
		n = read(fd_ra, &ev, sizeof ev);
		if (n == -1) {
			if (errno == EINTR)
				return (void *) 0;
			else
				break;
		} else if (n != sizeof ev) {
			errno = EIO;
			break;
		}

		// transform into KEYDOWN event
		switch (ev.value) {
		case +1:
			ev.type = EV_KEY;
			ev.value = 1;
			ev.code = KEY_VOLUMEUP;
			break;
		case -1:
			ev.type = EV_KEY;
			ev.value = 1;
			ev.code = KEY_VOLUMEDOWN;
			break;
		}

		xlog("ROTARY: distributing axis %s (0x%0x)", devinput_keyname(ev.code), ev.code);
#ifndef LOCALMAIN
		dac_handle(ev);
#endif
	}

	xlog("ROTARY axis error %s", strerror(errno));
	return (void *) 0;
}

static void *rotary_button(void *arg) {
	struct input_event ev;
	int n;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	while (1) {
		n = read(fd_rb, &ev, sizeof ev);
		if (n == -1) {
			if (errno == EINTR)
				return (void *) 0;
			else
				break;
		} else if (n != sizeof ev) {
			errno = EIO;
			break;
		}

		xlog("ROTARY: distributing button %s (0x%0x)", devinput_keyname(ev.code), ev.code);
#ifndef LOCALMAIN
		dac_handle(ev);
#endif
	}

	xlog("ROTARY button error %s", strerror(errno));
	return (void *) 0;
}

#ifdef LOCALMAIN

int main(void) {
	rotary_init();
	int c = getchar();
	rotary_close();
}

#endif
