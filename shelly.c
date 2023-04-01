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

static shelly_state_t *shelly_state = NULL;

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
		return (unsigned int) l;
	}

	return 0;
}

// find existing shelly state or create a new one
static shelly_state_t* get_shelly_state(unsigned int id, int relay) {
	shelly_state_t *ss = shelly_state;
	while (ss != NULL) {
		if (ss->id == id && ss->relay == relay)
			return ss;
		ss = ss->next;
	}

	shelly_state_t *ss_new = malloc(sizeof(shelly_state_t));
	ZERO(ss_new);
	ss_new->id = id;
	ss_new->relay = relay;

	if (shelly_state == NULL)
		// this is the head
		shelly_state = ss_new;
	else {
		// append to last in chain
		ss = shelly_state;
		while (ss->next != NULL)
			ss = ss->next;
		ss->next = ss_new;
	}

	xlog("SHELLY created new state for %06X %d", ss_new->id, ss_new->relay);
	return ss_new;
}

// update shelly state
static void update(unsigned int id, int relay, int state) {
	shelly_state_t *ss = get_shelly_state(id, relay);
	ss->state = state;
	xlog("SHELLY updated %6X relay %d state to %d", ss->id, ss->relay, ss->state);
}

// trigger a button press event
static void trigger(unsigned int id, int button, int action) {
	if (!action)
		return; // we do not track button 'release', only button 'press'

	xlog("SHELLY trigger %6X %d %d", id, button, action);
	for (int i = 0; i < ARRAY_SIZE(shelly_config); i++) {
		shelly_config_t sc = shelly_config[i];
		shelly_state_t *ss = get_shelly_state(sc.id, sc.relay);

		if (sc.t1 == id && sc.t1b == button) {
			if (ss->state == 0)
				shelly_command(sc.id, sc.relay, 1);
			else
				shelly_command(sc.id, sc.relay, 0);
		}

		if (sc.t2 == id && sc.t2b == button) {
			if (ss->state == 0)
				shelly_command(sc.id, sc.relay, 1);
			else
				shelly_command(sc.id, sc.relay, 0);
		}

		if (sc.t3 == id && sc.t3b == button) {
			if (ss->state == 0)
				shelly_command(sc.id, sc.relay, 1);
			else
				shelly_command(sc.id, sc.relay, 0);
		}

		if (sc.t4 == id && sc.t4b == button) {
			if (ss->state == 0)
				shelly_command(sc.id, sc.relay, 1);
			else
				shelly_command(sc.id, sc.relay, 0);
		}
	}
}

// handle a subscribed mqtt message
int shelly_dispatch(const char *topic, uint16_t tsize, const char *message, size_t msize) {
	char fmt[32], s[32], a[5];

	unsigned int id = get_id(topic, tsize);
	if (!id)
		return 0;

	// xlog("SHELLY dispatch %6X %s", id, message);

	// search for button action commands
	for (int i = 0; i < 8; i++) {
		snprintf(fmt, 32, "{Switch%d:%%s}", i);
		if (json_scanf(message, msize, fmt, &s)) {
			if (!strcmp(s, ON))
				trigger(id, i, 1); // Shelly1+2
			else if (!strcmp(a, OFF))
				trigger(id, i, 0); // Shelly1+2
			else if (json_scanf(s, strlen(s), "{Action:%s}", &a))
				trigger(id, i, !strcmp(a, ON) ? 1 : 0); // Shelly4
		}
	}

	// search for relay power state results
	if (json_scanf(message, msize, "{POWER:%s}", &a))
		update(id, 0, !strcmp(a, ON) ? 1 : 0);
	if (json_scanf(message, msize, "{POWER1:%s}", &a))
		update(id, 1, !strcmp(a, ON) ? 1 : 0);
	if (json_scanf(message, msize, "{POWER2:%s}", &a))
		update(id, 2, !strcmp(a, ON) ? 1 : 0);

	return 0;
}

// execute a shelly command via mqtt publish
void shelly_command(unsigned int id, int relay, int cmd) {
	char *t = (char*) malloc(32);

	if (relay)
		snprintf(t, 32, "shelly/%6X/cmnd/POWER%d", id, relay);
	else
		snprintf(t, 32, "shelly/%6X/cmnd/POWER", id);

	if (cmd) {
		xlog("SHELLY switching %6X %d %s", id, relay, ON);
		publish(t, ON);

		// start timer if configured
		for (int i = 0; i < ARRAY_SIZE(shelly_config); i++) {
			shelly_config_t sc = shelly_config[i];
			if (sc.id == id && sc.relay == relay)
				if (sc.timer) {
					shelly_state_t *ss = get_shelly_state(sc.id, sc.relay);
					ss->timer = sc.timer;
					xlog("SHELLY started timer for %06X %d %d", ss->id, ss->relay, ss->timer);
				}
		}
	} else {
		xlog("SHELLY switching %6X %d %s", id, relay, OFF);
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
		shelly_state_t *ss = shelly_state;
		while (ss != NULL) {
			// xlog("SHELLY state %06X %d %d %d ", ss->id, ss->relay, ss->state, ss->timer);
			if (ss->timer) {
				ss->timer--;
				if (ss->timer == 0)
					shelly_command(ss->id, ss->relay, 0);
			}
			ss = ss->next;
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

