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

int fd_rotary;

int rotary_init() {
	char name[256] = "Unknown";

	// Open Devices
	if ((fd_rotary = open(DEVINPUT_ROTARY, O_RDONLY)) == -1) {
		mcplog("unable to open %s", DEVINPUT_ROTARY);
	}

	// Print Device Name
	ioctl(fd_rotary, EVIOCGNAME(sizeof(name)), name);
	mcplog("ROTARY: reading from %s (%s)", DEVINPUT_ROTARY, name);

	return 0;
}

void rotary_close() {
	if (fd_rotary) {
		close(fd_rotary);
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
			dac_volume_up();
		} else if (ev.value == +1) {
			dac_volume_down();
		}
		display_update();
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
