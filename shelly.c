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

static shelly_t* get_shelly(unsigned int id, int relay) {
	for (int i = 0; i < ARRAY_SIZE(shelly_config); i++) {
		shelly_t sc = shelly_config[i];
		if (sc.id == id && sc.relay == relay)
			return &shelly_config[i];
	}
	return NULL;
}

static void update(unsigned int id, int relay, int state) {
	shelly_t *sc = get_shelly(id, relay);
	if (sc == NULL)
		return;

	sc->state = state;
	xlog("SHELLY updated %6X relay %d power state to %d", sc->id, sc->relay, sc->state);
}

static void trigger(unsigned int id, int button, int action) {
	xlog("SHELLY trigger %6X %d %d", id, button, action);

	for (int i = 0; i < ARRAY_SIZE(shelly_config); i++) {
		shelly_t sc = shelly_config[i];

		if (sc.t1 == id && sc.t1b == button)
			shelly_command(sc.id, sc.state == 0 ? 1 : 0);

		if (sc.t2 == id && sc.t2b == button)
			shelly_command(sc.id, sc.state == 0 ? 1 : 0);

		if (sc.t3 == id && sc.t3b == button)
			shelly_command(sc.id, sc.state == 0 ? 1 : 0);

		if (sc.t4 == id && sc.t4b == button)
			shelly_command(sc.id, sc.state == 0 ? 1 : 0);
	}
}

int shelly_dispatch(const char *topic, uint16_t tsize, const char *message, size_t msize) {
	char fmt[32], s[32], a[5];

	unsigned int id = get_id(topic, tsize);
	// xlog("SHELLY dispatch %6X %s", id, message);

	// search for button action commands
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

	// search for power state results
	if (json_scanf(message, msize, "{POWER:%s}", &a))
		update(id, 0, !strcmp(a, ON) ? 1 : 0);
	if (json_scanf(message, msize, "{POWER1:%s}", &a))
		update(id, 1, !strcmp(a, ON) ? 1 : 0);
	if (json_scanf(message, msize, "{POWER2:%s}", &a))
		update(id, 2, !strcmp(a, ON) ? 1 : 0);

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

