#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include "utils.h"
#include "mcp.h"

static pthread_t thread;

static void* template(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		msleep(1000);

		// do some fancy stuff in a loop

		xlog("template loop");
	}
}

static int init() {

	// initialize this module

	if (pthread_create(&thread, NULL, &template, NULL))
		return xerr("Error creating template thread");

	xlog("TEMPLATE initialized");
	return 0;
}

static void destroy() {
	if (pthread_cancel(thread))
		xlog("Error canceling template thread");

	if (pthread_join(thread, NULL))
		xlog("Error joining template thread");

	// destroy this module
}

MCP_REGISTER(template, 99, &init, &destroy);
