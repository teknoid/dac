#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include "utils.h"
#include "mcp.h"

static void loop() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	while (1) {
		sleep(1);

		// do some fancy stuff in a loop

		xlog("template loop");
	}
}

static int init() {

	// initialize this module

	return 0;
}

static void stop() {

	// stop and destroy this module
}

static int test() {
	xlog("module test code");
	return 0;
}

int template_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	// no arguments - main loop
	if (argc == 1) {
		init();
		loop();
		pause();
		stop();
		return 0;
	}

	// with arguments
	int c;
	while ((c = getopt(argc, argv, "t")) != -1) {
		switch (c) {
		case 't':
			return test();
		}
	}

	return 0;
}

#ifdef TEMPLATE_MAIN
int main(int argc, char **argv) {
	return template_main(argc, argv);
}
#else
MCP_REGISTER(template, 99, &init, &stop, &loop);
#endif
