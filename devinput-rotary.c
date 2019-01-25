#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/input.h>
#include <linux/uinput.h>

#include "mcp.h"

#define DEVINPUT_ROTARY			"/dev/input/event1"
#define DEVINPUT_UINPUT			"/dev/uinput"

// #define LOCALMAIN

int fd_udev;
int fd_rotary;

static void udev(int type, int code, int val) {
	struct input_event ie;

	ie.type = type;
	ie.code = code;
	ie.value = val;
	ie.time.tv_sec = 0;
	ie.time.tv_usec = 0;
	write(fd_udev, &ie, sizeof(ie));
}

static void fire(int key) {
	udev(EV_KEY, key, 1);
	udev(EV_SYN, SYN_REPORT, 0);
	udev(EV_KEY, key, 0);
	udev(EV_SYN, SYN_REPORT, 0);
}

int rotary_init() {
	char name[256] = "Unknown";
	struct uinput_setup usetup;

	// Open Devices
	if ((fd_rotary = open(DEVINPUT_ROTARY, O_RDONLY)) == -1) {
		mcplog("unable to open %s", DEVINPUT_ROTARY);
	}
	if ((fd_udev = open(DEVINPUT_UINPUT, O_WRONLY | O_NONBLOCK)) == -1) {
		mcplog("unable to open %s", DEVINPUT_UINPUT);
	}

	// Print Device Name
	ioctl(fd_rotary, EVIOCGNAME(sizeof(name)), name);
	mcplog("ROTARY: reading from %s (%s)", DEVINPUT_ROTARY, name);

	/*
	 * The ioctls below will enable the device that is about to be
	 * created, to pass key events, in this case the + and - keys.
	 */
	ioctl(fd_udev, UI_SET_EVBIT, EV_KEY);
	ioctl(fd_udev, UI_SET_KEYBIT, KEY_KPPLUS);
	ioctl(fd_udev, UI_SET_KEYBIT, KEY_KPMINUS);

	memset(&usetup, 0, sizeof(usetup));
	usetup.id.bustype = BUS_USB;
	usetup.id.vendor = 0x1912; /* sample vendor */
	usetup.id.product = 0x1975; /* sample product */
	strcpy(usetup.name, "Rotary +/-");

	ioctl(fd_udev, UI_DEV_SETUP, &usetup);
	ioctl(fd_udev, UI_DEV_CREATE);

	return 0;
}

void rotary_close() {
	if (fd_rotary) {
		close(fd_rotary);
	}
	if (fd_udev) {
		ioctl(fd_udev, UI_DEV_DESTROY);
		close(fd_udev);
	}
}

void *rotary(void *arg) {
	struct input_event ev;
	int n;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		mcplog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	while (1) {
		n = read(fd_rotary, &ev, sizeof ev);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			else
				break;
		} else if (n != sizeof ev) {
			errno = EIO;
			break;
		}

		if (ev.value == -1) {
//			fire(KEY_KPPLUS);
			dac_volume_up();
		} else if (ev.value == +1) {
//			fire(KEY_KPMINUS);
			dac_volume_down();
		}
	}

	mcplog("ROTARY error", strerror(errno));
	return (void *) 0;
}

#ifdef LOCALMAIN

int main(void) {
	rotary_init();
	rotary(NULL);
	rotary_close();
}

#endif
