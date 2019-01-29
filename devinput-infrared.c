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

#ifndef DEVINPUT_IR
#define DEVINPUT_IR			"/dev/input/infrared"
#endif

// #define LOCALMAIN

static int fd_ir;
static pthread_t thread_ir;
static void *ir(void *arg);

int ir_init() {
	char name[256] = "";
	unsigned int repeat[2];

	// Open Device
	if ((fd_ir = open(DEVINPUT_IR, O_RDONLY)) == -1) {
		xlog("unable to open %s", DEVINPUT_IR);
	}

	// Print Device Name
	ioctl(fd_ir, EVIOCGNAME(sizeof(name)), name);
	xlog("INFRARED: reading from %s (%s)", DEVINPUT_IR, name);

	// set repeat rate
	ioctl(fd_ir, EVIOCGREP, repeat);
	xlog("delay = %d; repeat = %d", repeat[REP_DELAY], repeat[REP_PERIOD]);
	repeat[REP_DELAY] = 400;
	repeat[REP_PERIOD] = 200;
	ioctl(fd_ir, EVIOCSREP, repeat);
	ioctl(fd_ir, EVIOCGREP, repeat);
	xlog("delay = %d; repeat = %d", repeat[REP_DELAY], repeat[REP_PERIOD]);

	// start listener
	if (pthread_create(&thread_ir, NULL, &ir, NULL)) {
		xlog("Error creating thread_ir");
	}

	return 0;
}

void ir_close() {
	if (pthread_cancel(thread_ir)) {
		xlog("Error canceling thread_ir");
	}
	if (pthread_join(thread_ir, NULL)) {
		xlog("Error joining thread_ir");
	}
	if (fd_ir) {
		close(fd_ir);
	}
}

static void *ir(void *arg) {
	struct input_event ev;
	int n, seq;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	while (1) {
		n = read(fd_ir, &ev, sizeof ev);
		if (n == -1) {
			if (errno == EINTR)
				return (void *) 0;
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

		ev.value = seq; // abuse value for sequence
		xlog("INFRARED: distributing key %s (0x%0x)", devinput_keyname(ev.code), ev.code);
#ifndef LOCALMAIN
		dac_handle(ev);
#endif
	}

	xlog("INFRARED error %s", strerror(errno));
	return (void *) 0;
}

#ifdef LOCALMAIN

int main(void) {
	ir_init();
	int c = getchar();
	ir_close();
}

#endif

