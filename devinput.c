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

int fd;

int find_key(char *name) {
	struct parse_event *p;
	for (p = key_events; p->name != NULL; p++) {
		if (!strcmp(name, p->name)) {
			return p->value;
		}
	}
	return 0;
}

char *get_key_name(unsigned int key) {
	struct parse_event *p;
	for (p = key_events; p->name != NULL; p++) {
		if (key == p->value) {
			return p->name;
		}
	}
	return NULL;
}

void* devinput(void *arg) {
	struct input_event ev;
	int n, seq;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		mcplog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	while (1) {
		n = read(fd, &ev, sizeof ev);
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
		} else if (ev.code == KEY_SELECT && seq == 0) {
			dac_select_channel();
		} else if (ev.code == KEY_POWER && seq == 0) {
			power_soft();
		} else if (ev.code == KEY_POWER && seq == 10) {
			power_hard();
		} else if (seq == 0) {
			mpdclient_handle(ev.code);
		}
	}
	mcplog("DEVINPUT error", strerror(errno));
	return (void *) 0;
}

int devinput_init() {
#ifdef DEVINPUT
	char name[256] = "Unknown";
	unsigned int repeat[2];

//Open Device
	if ((fd = open(DEVINPUT, O_RDONLY)) == -1) {
		mcplog("unable to open %s", DEVINPUT);
	}

//Print Device Name
	ioctl(fd, EVIOCGNAME(sizeof(name)), name);
	mcplog("reading from : %s (%s)", DEVINPUT, name);

// set repeat rate
	ioctl(fd, EVIOCGREP, repeat);
	mcplog("delay = %d; repeat = %d", repeat[REP_DELAY], repeat[REP_PERIOD]);
	repeat[REP_DELAY] = 400;
	repeat[REP_PERIOD] = 200;
	ioctl(fd, EVIOCSREP, repeat);
	ioctl(fd, EVIOCGREP, repeat);
	mcplog("delay = %d; repeat = %d", repeat[REP_DELAY], repeat[REP_PERIOD]);

#endif
	return 0;
}

