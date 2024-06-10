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

MCP_REGISTER(template, 99, &init, &stop, &loop);
