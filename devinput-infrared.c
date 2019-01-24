#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/input.h>

#include "mcp.h"
#include "keytable.h"

int fd_ir;

char *devinput_keyname(unsigned int key) {
	struct parse_event *p;
	for (p = key_events; p->name != NULL; p++) {
		if (key == p->value) {
			return p->name;
		}
	}
	return NULL;
}

int devinput_find_key(const char *name) {
	struct parse_event *p;
	for (p = key_events; p->name != NULL; p++) {
		if (!strcmp(name, p->name)) {
			return p->value;
		}
	}
	return 0;
}

int devinput_init() {
	char name[256] = "Unknown";
	unsigned int repeat[2];

	// Open Device
	if ((fd_ir = open(DEVINPUT, O_RDONLY)) == -1) {
		mcplog("unable to open %s", DEVINPUT);
	}

	// Print Device Name
	ioctl(fd_ir, EVIOCGNAME(sizeof(name)), name);
	mcplog("INFRARED: reading from %s (%s)", DEVINPUT, name);

	// set repeat rate
	ioctl(fd_ir, EVIOCGREP, repeat);
	mcplog("delay = %d; repeat = %d", repeat[REP_DELAY], repeat[REP_PERIOD]);
	repeat[REP_DELAY] = 400;
	repeat[REP_PERIOD] = 200;
	ioctl(fd_ir, EVIOCSREP, repeat);
	ioctl(fd_ir, EVIOCGREP, repeat);
	mcplog("delay = %d; repeat = %d", repeat[REP_DELAY], repeat[REP_PERIOD]);
	return 0;
}

void devinput_close() {
	if (fd_ir) {
		close(fd_ir);
	}
}

void *devinput(void *arg) {
	struct input_event ev;
	int n, seq;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		mcplog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	while (1) {
		n = read(fd_ir, &ev, sizeof ev);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			else
				break;
		} else if (n != sizeof ev) {
			errno = EIO;
			break;
		}

		if (ev.type != EV_KEY) {
			continue;
		}

		switch (ev.value) {
		case 0: // RELEASE
			seq = 0;
			continue;
		case 1: // PRESS
			seq = 0;
			break;
		case 2: // REPEAT
			if (seq++ < 4)
				continue; // skip the always coming first repeat, bug ???
			break;
		}

		// mcplog("DEVINPUT %d %d %02d %s", ev.type, ev.value, seq, get_key_name(ev.code));
		if (ev.code == KEY_VOLUMEUP) {
			dac_volume_up();
		} else if (ev.code == KEY_VOLUMEDOWN) {
			dac_volume_down();
		} else if (ev.code == KEY_POWER && seq == 0) {
			power_soft();
		} else if (ev.code == KEY_POWER && seq == 10) {
			power_hard();
		} else if (seq == 0) {
			mcplog("INFRARED: distributing key %s (0x%0x)", devinput_keyname(ev.code), ev.code);
			dac_handle(ev.code);
			mpdclient_handle(ev.code);
		}

#ifdef DISPLAY
		display_update();
#endif

	}
	mcplog("INFRARED error", strerror(errno));
	return (void *) 0;
}
