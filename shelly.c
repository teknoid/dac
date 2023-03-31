#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <pthread.h>

#include "mcp.h"
#include "mqtt.h"
#include "utils.h"
#include "frozen.h"
#include "shelly-config.h"

static pthread_t thread;

// shelly/B20670/stat/RESULT {"Switch3":{"Action":"OFF"}}
//        ^^^^^^
static unsigned int get_id(const char *topic, size_t size) {
	int slash1 = 0, slash2 = 0;
	for (int i = 0; i < size; i++) {
		if (topic[i] == '/') {
			if (!slash1)
				slash1 = i;
			else if (!slash2)
				slash2 = i;
		}
	}

	if (slash1 && slash2 && ((slash2 - slash1) == 7)) {
		char id[7];
		memcpy(id, &topic[slash1 + 1], 6);
		id[6] = '\0';
		long l = strtol(id, NULL, 16);
		// xlog("ID %s => 0x%2x", id, l);
		return (unsigned int) l;
	}

	return 0;
}

static void trigger(unsigned int id, int button, int action) {
	xlog("SHELLY trigger %6X %d %d", id, button, action);

	// TODO Race condition - as long as we are in callback sending is not possible
	// find a mechanism to enqueue this message for sending later after leaving the callback

//	if (id == VIER_KUECHE && button == 1)
//		shelly_command(PLUG1, 0);
}

int shelly_dispatch(const char *topic, uint16_t tsize, const char *message, size_t msize) {
	char fmt[32], s[32], a[5];

	unsigned int id = get_id(topic, tsize);
	for (int i = 0; i < 8; i++) {
		snprintf(fmt, 32, "{Switch%d:%%s}", i);
		if (json_scanf(message, msize, fmt, &s)) {
			if (json_scanf(s, strlen(s), "{Action:%s}", &a)) {
				if (!strcmp(a, ON))
					trigger(id, i, 1);
				else if (!strcmp(a, OFF))
					trigger(id, i, 0);
			}
		}
	}

	return 0;
}

void shelly_command(unsigned int id, int cmd) {
	char *t = (char*) malloc(32);
	snprintf(t, 32, "shelly/%6X/cmnd/POWER", id);
	if (cmd) {
		xlog("SHELLY switching %6X %s", id, ON);
		publish(t, ON);

		// start timer if configured
		for (int i = 0; i < ARRAY_SIZE(shelly_config); i++) {
			shelly_t sc = shelly_config[i];
			if (sc.id == id)
				if (sc.timer_start)
					sc.timer = sc.timer_start;
		}
	} else {
		xlog("SHELLY switching %6X %s", id, OFF);
		publish(t, OFF);
	}
	free(t);
}

static void* loop(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		sleep(1);

		// decrease timers and switch off if timer reached 0
		for (int i = 0; i < ARRAY_SIZE(shelly_config); i++) {
			shelly_t sc = shelly_config[i];
			if (sc.timer_start && sc.timer)
				if (sc.timer--)
					shelly_command(sc.id, 0);
		}
	}
}

static int init() {
	if (pthread_create(&thread, NULL, &loop, NULL))
		return xerr("Error creating shelly thread");

	xlog("SHELLY initialized");
	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("Error canceling shelly thread");

	if (pthread_join(thread, NULL))
		xlog("Error joining shelly thread");
}

MCP_REGISTER(shelly, 3, &init, &stop);

