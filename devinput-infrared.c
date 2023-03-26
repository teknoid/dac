#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#include <sys/ioctl.h>

#include <linux/input.h>
#include <linux/input-event-codes.h>

#include "mcp.h"
#include "utils.h"

#ifndef DEVINPUT_IR
#define DEVINPUT_IR			"/dev/input/infrared"
#endif

// #define LOCALMAIN

static int fd_ir;
static pthread_t thread;

static void* ir(void *arg) {
	struct input_event ev;
	int n, seq;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		n = read(fd_ir, &ev, sizeof(ev));
		if (n == -1) {
			if (errno == EINTR)
				return (void*) 0;
			else
				break;
		} else if (n != sizeof(ev)) {
			errno = EIO;
			break;
		}

		if (ev.type != EV_KEY) {
			continue;
		}

		if (!mcp->ir_active) {
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
		dac_handle(ev.code);
#endif
	}

	xlog("INFRARED error %s", strerror(errno));
	return (void*) 0;
}

static int init() {
	char name[256] = "";
	unsigned int repeat[2];

	// Open Device
	if ((fd_ir = open(DEVINPUT_IR, O_RDONLY)) == -1) {
		xlog("unable to open %s", DEVINPUT_IR);
		return -1;
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
	if (pthread_create(&thread, NULL, &ir, NULL)) {
		xlog("Error creating thread_ir");
		return -1;
	}

	xlog("INFARRED initialized");
	return 0;
}

static void destroy() {
	if (pthread_cancel(thread))
		xlog("Error canceling thread_ir");

	if (pthread_join(thread, NULL))
		xlog("Error joining thread_ir");

	if (fd_ir)
		close(fd_ir);
}

MCP_REGISTER(ir, 3, &init, &destroy);

#ifdef LOCALMAIN

int main(void) {
	init();
	int c = getchar();
	destroy();
}

#endif

