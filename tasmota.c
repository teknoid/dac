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
#include "tasmota-config.h"

static pthread_t thread;

static tasmota_state_t *tasmota_state = NULL;

// tasmota/B20670/stat/RESULT {"Switch3":{"Action":"OFF"}}
//         ^^^^^^
static unsigned int get_id(const char *topic, size_t size) {
	int slash1 = 0, slash2 = 0;

	for (int i = 0; i < size; i++)
		if (topic[i] == '/') {
			if (!slash1)
				slash1 = i;
			else if (!slash2)
				slash2 = i;
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

// find existing tasmota state or create a new one
static tasmota_state_t* get_tasmota_state(unsigned int id, int relay) {
	tasmota_state_t *ss = tasmota_state;
	while (ss != NULL) {
		if (ss->id == id && ss->relay == relay)
			return ss;
		ss = ss->next;
	}

	tasmota_state_t *ss_new = malloc(sizeof(tasmota_state_t));
	ZERO(ss_new);
	ss_new->id = id;
	ss_new->relay = relay;

	if (tasmota_state == NULL)
		// this is the head
		tasmota_state = ss_new;
	else {
		// append to last in chain
		ss = tasmota_state;
		while (ss->next != NULL)
			ss = ss->next;
		ss->next = ss_new;
	}

	xlog("TASMOTA created new state for %06X %d", ss_new->id, ss_new->relay);
	return ss_new;
}

// update tasmota state
static void update(unsigned int id, int relay, int state) {
	tasmota_state_t *ss = get_tasmota_state(id, relay);
	ss->state = state;
	xlog("TASMOTA updated %6X relay %d state to %d", ss->id, ss->relay, ss->state);
}

// trigger a button press event
static void trigger(unsigned int id, int button, int action) {
	if (!action)
		return; // we do not track button 'release', only button 'press'

	xlog("TASMOTA trigger %6X %d %d", id, button, action);
	for (int i = 0; i < ARRAY_SIZE(tasmota_config); i++) {
		tasmota_config_t sc = tasmota_config[i];
		tasmota_state_t *ss = get_tasmota_state(sc.id, sc.relay);

		if (sc.t1 == id && sc.t1b == button) {
			if (ss->state == 0)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}

		if (sc.t2 == id && sc.t2b == button) {
			if (ss->state == 0)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}

		if (sc.t3 == id && sc.t3b == button) {
			if (ss->state == 0)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}

		if (sc.t4 == id && sc.t4b == button) {
			if (ss->state == 0)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}
	}
}

// handle a subscribed mqtt message
int tasmota_dispatch(const char *topic, uint16_t tsize, const char *message, size_t msize) {
	char fmt[32], s[32], a[5];

	unsigned int id = get_id(topic, tsize);
	if (!id)
		return 0;

	// xlog("TASMOTA dispatch %6X %s", id, message);

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

// execute tasmota POWER command via mqtt publish
void tasmota_power(unsigned int id, int relay, int cmd) {
	char topic[32];

	if (relay)
		snprintf(topic, 32, "tasmota/%6X/cmnd/POWER%d", id, relay);
	else
		snprintf(topic, 32, "tasmota/%6X/cmnd/POWER", id);

	if (cmd) {
		xlog("TASMOTA switching %6X %d %s", id, relay, ON);
		publish(topic, ON);

		// start timer if configured
		for (int i = 0; i < ARRAY_SIZE(tasmota_config); i++) {
			tasmota_config_t sc = tasmota_config[i];
			if (sc.id == id && sc.relay == relay)
				if (sc.timer) {
					tasmota_state_t *ss = get_tasmota_state(sc.id, sc.relay);
					ss->timer = sc.timer;
					xlog("TASMOTA started timer for %06X %d %d", ss->id, ss->relay, ss->timer);
				}
		}
	} else {
		xlog("TASMOTA switching %6X %d %s", id, relay, OFF);
		publish(topic, OFF);
	}
}

// execute tasmota BACKLOG command via mqtt publish
void tasmota_backlog(unsigned int id, const char *cmd) {
	char topic[32];
	snprintf(topic, 32, "tasmota/%6X/cmnd/Backlog", id);
	xlog("TASMOTA executing backlog command %6X %s", id, cmd);
	publish(topic, cmd);
}

static void* loop(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		sleep(1);

		// decrease timers and switch off if timer reached 0
		tasmota_state_t *ss = tasmota_state;
		while (ss != NULL) {
			// xlog("TASMOTA state %06X %d %d %d ", ss->id, ss->relay, ss->state, ss->timer);
			if (ss->timer) {
				ss->timer--;
				if (ss->timer == 0)
					tasmota_power(ss->id, ss->relay, 0);
			}
			ss = ss->next;
		}
	}
}

static int init() {
	if (pthread_create(&thread, NULL, &loop, NULL))
		return xerr("Error creating tasmota thread");

	xlog("TASMOTA initialized");
	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("Error canceling tasmota thread");

	if (pthread_join(thread, NULL))
		xlog("Error joining tasmota thread");
}

MCP_REGISTER(tasmota, 3, &init, &stop);
