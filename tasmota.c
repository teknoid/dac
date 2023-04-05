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
static tasmota_state_t* get_state(unsigned int id) {
	tasmota_state_t *ss = tasmota_state;
	while (ss != NULL) {
		if (ss->id == id)
			return ss;
		ss = ss->next;
	}

	tasmota_state_t *ss_new = malloc(sizeof(tasmota_state_t));
	ss_new->id = id;
	ss_new->relay1 = -1;
	ss_new->relay2 = -1;
	ss_new->position = -1;
	ss_new->timer = 0;
	ss_new->next = NULL;

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

	xlog("TASMOTA created new state for %06X", ss_new->id);
	return ss_new;
}

// execute tasmota BACKLOG command via mqtt publish
static void backlog(unsigned int id, const char *cmd) {
	char topic[32];
	snprintf(topic, 32, "tasmota/%6X/cmnd/Backlog", id);
	xlog("TASMOTA executing backlog command %6X %s", id, cmd);
	publish(topic, cmd);
}

// update tasmota relay power state
static void update_relay(unsigned int id, int relay, int power) {
	tasmota_state_t *ss = get_state(id);
	if (relay == 0 || relay == 1) {
		ss->relay1 = power;
		xlog("TASMOTA updated %6X relay1 state to %d", ss->id, power);
	} else if (relay == 2) {
		ss->relay2 = power;
		xlog("TASMOTA updated %6X relay2 state to %d", ss->id, power);
	} else
		xlog("TASMOTA no relay%d at %6X", ss->id, relay);
}

// update tasmota shutter position
static void update_shutter(unsigned int id, unsigned int position) {
	tasmota_state_t *ss = get_state(id);
	ss->position = position;
	xlog("TASMOTA updated %6X shutter position to %d", ss->id, position);
}

// trigger a button press event
static void trigger(unsigned int id, int button, int action) {
	xlog("TASMOTA trigger %6X %d %d", id, button, action);

	// we do not track button 'release', only button 'press'
	if (!action)
		return;

	for (int i = 0; i < ARRAY_SIZE(tasmota_config); i++) {
		tasmota_config_t sc = tasmota_config[i];
		tasmota_state_t *ss = get_state(sc.id);
		int power = (sc.relay == 0 || sc.relay == 1) ? ss->relay1 : ss->relay2;

		if (sc.t1 == id && sc.t1b == button) {
			if (power != 1)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}

		if (sc.t2 == id && sc.t2b == button) {
			if (power != 1)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}

		if (sc.t3 == id && sc.t3b == button) {
			if (power != 1)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}

		if (sc.t4 == id && sc.t4b == button) {
			if (power != 1)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}
	}
}

// handle a subscribed mqtt message
void tasmota_dispatch(const char *topic, uint16_t tsize, const char *message, size_t msize) {
	char fmt[32], a[5];
	int i;

	// we are only interested in RESULT messages
	if (!ends_with("RESULT", topic, tsize))
		return;

	unsigned int id = get_id(topic, tsize);
	if (!id)
		return;

	// scan for button action commands
	for (int i = 0; i < 8; i++) {
		snprintf(fmt, 32, "{Switch%d:%%Q}", i);
		char *sw = NULL;
		if (json_scanf(message, msize, fmt, &sw)) {
			if (!strcmp(sw, ON))
				trigger(id, i, 1); // Shelly1+2
			else if (!strcmp(sw, OFF))
				trigger(id, i, 0); // Shelly1+2
			else {
				if (json_scanf(sw, strlen(sw), "{Action:%s}", &a)) {
					// Shelly4
					if (!strcmp(a, ON))
						trigger(id, i, 1);
					else
						trigger(id, i, 0);
				}
			}
			free(sw);
		}
	}

	// scan for relay power state results
	if (json_scanf(message, msize, "{POWER:%s}", &a))
		update_relay(id, 0, !strcmp(a, ON) ? 1 : 0);
	if (json_scanf(message, msize, "{POWER1:%s}", &a))
		update_relay(id, 1, !strcmp(a, ON) ? 1 : 0);
	if (json_scanf(message, msize, "{POWER2:%s}", &a))
		update_relay(id, 2, !strcmp(a, ON) ? 1 : 0);

	// scan for shutter position results
	char *sh = NULL;
	if (json_scanf(message, msize, "{Shutter1:%Q}", &sh)) {
		if (json_scanf(sh, strlen(sh), "{Position:%d}", &i))
			update_shutter(id, i);
		free(sh);
	}
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
			if (sc.id == id)
				if (sc.timer) {
					tasmota_state_t *ss = get_state(sc.id);
					ss->timer = sc.timer;
					xlog("TASMOTA started timer for %06X %d", ss->id, ss->timer);
				}
		}
	} else {
		xlog("TASMOTA switching %6X %d %s", id, relay, OFF);
		publish(topic, OFF);
	}
}

// execute tasmota shutter up/down
void tasmota_shutter(unsigned int id, unsigned int move) {
	if (move == SHUTTER_DOWN)
		backlog(id, "ShutterClose");
	else if (move == SHUTTER_HALF) {
		backlog(id, "ShutterClose");
		sleep(15);
		backlog(id, "ShutterStop");
	} else if (move == SHUTTER_UP)
		backlog(id, "ShutterOpen");
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
			if (ss->timer) {
				ss->timer--;
				if (ss->timer == 0) {
					if (ss->relay1)
						tasmota_power(ss->id, 0, 0);
					if (ss->relay2)
						tasmota_power(ss->id, 2, 0);
				}
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

