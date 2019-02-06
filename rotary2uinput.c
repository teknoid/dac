#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEVINPUT_ROTARY			"/dev/input/by-path/platform-rotary_axis-event"
#define DEVINPUT_UINPUT			"/dev/uinput"

#define ROTARY_KEY_CW			KEY_KPPLUS
#define ROTARY_KEY_CCW			KEY_KPMINUS

static void event(int fd, int type, int code, int val) {
	struct input_event ie;

	ie.type = type;
	ie.code = code;
	ie.value = val;
	ie.time.tv_sec = 0;
	ie.time.tv_usec = 0;
	write(fd, &ie, sizeof(ie));
}

static void fire(int fd, int key) {
	event(fd, EV_KEY, key, 1);
	event(fd, EV_SYN, SYN_REPORT, 0);
	event(fd, EV_KEY, key, 0);
	event(fd, EV_SYN, SYN_REPORT, 0);
}

int main(void) {
	char name[256] = "Unknown";
	struct uinput_setup usetup;
	struct input_event ev;
	int fd_udev, fd_rotary, n;

	// Open Devices
	if ((fd_rotary = open(DEVINPUT_ROTARY, O_RDONLY)) == -1) {
		perror("unable to open " DEVINPUT_ROTARY);
	}
	if ((fd_udev = open(DEVINPUT_UINPUT, O_WRONLY | O_NONBLOCK)) == -1) {
		perror("unable to open " DEVINPUT_UINPUT);
	}

	// Print Device Name
	ioctl(fd_rotary, EVIOCGNAME(sizeof(name)), name);
	printf("ROTARY: reading from %s (%s)", DEVINPUT_ROTARY, name);

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

	// listen for rotary events
	while (1) {
		n = read(fd_rotary, &ev, sizeof(ev));
		if (n == -1) {
			if (errno == EINTR)
				continue;
			else
				break;
		} else if (n != sizeof(ev)) {
			errno = EIO;
			break;
		}

		if (ev.value == -1) {
			fire(fd_udev, ROTARY_KEY_CW);
		} else if (ev.value == +1) {
			fire(fd_udev, ROTARY_KEY_CCW);
		}
	}

	if (fd_rotary) {
		close(fd_rotary);
	}

	if (fd_udev) {
		ioctl(fd_udev, UI_DEV_DESTROY);
		close(fd_udev);
	}
}
