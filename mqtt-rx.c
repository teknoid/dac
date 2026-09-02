/*****************************************************************************

 mqtt receiver based on simple_subscriber.c
 https://github.com/LiamBindle/MQTT-C

 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include <posix_sockets.h>
#include <mqttc.h>

#include "sensors.h"
#include "tasmota.h"
#include "frozen.h"
#include "solar.h"
#include "utils.h"
#include "mqtt.h"
#include "mcp.h"

#ifndef MQTT_HOST
#define	MQTT_HOST				"localhost"
#endif

#ifndef MQTT_PORT
#define MQTT_PORT				"1883"
#endif

#define MAC_HANDY				0xfc539ea93ac5
#define DARKNESS				50

static int fd;
static struct mqtt_client *client = NULL;
static uint8_t sendbuf[4096];
static uint8_t recvbuf[1024];

static void dump(const char *prefix, struct mqtt_response_publish *p) {
	char *t = make_string(p->topic_name, p->topic_name_size);
	char *m = make_string(p->application_message, p->application_message_size);
	xlog("%s topic('%s') = %s", prefix, t, m);
	free(t);
	free(m);
}

// network/dhcp/fc:53:9e:a9:3a:c5 old 192.168.25.83 2023-04-02 13:16:40 (gigaset-hje)
//              ^^^^^^^^^^^^^^^^^
static uint64_t get_mac(const char *topic, size_t size) {
	int slash1 = 0, slash2 = 0;
	unsigned int a, b;

	for (int i = 0; i < size; i++)
		if (topic[i] == '/') {
			if (!slash1)
				slash1 = i;
			else if (!slash2)
				slash2 = i;
		}

	if (slash1 && slash2 && ((size - slash2) == 18)) {
		// TODO use utils mac2uint64()
		const char *c = topic + slash2 + 1;
		uint64_t x = 0;
		for (int i = 0; i < 6; i++) {
			a = (*c <= '9') ? *c - '0' : (*c & 0x7) + 9;
			c++; // 'f'
			b = (*c <= '9') ? *c - '0' : (*c & 0x7) + 9;
			c++; // 'c'
			x = (x << 8) | (a << 4) | b;
			c++; // ':'
		}
		return x;
	}

	return 0;
}

static int dispatch_notification(struct mqtt_response_publish *p) {
	const char *message = p->application_message;
	size_t msize = p->application_message_size;

	char *title = NULL, *text = NULL, *sound = NULL;
	json_scanf(message, msize, "{title:%Q, text:%Q, sound:%Q}", &title, &text, &sound);

	mcp_notify(title, text, sound, 0);

	free(title);
	free(text);
	free(sound);

	return 0;
}

static int dispatch_network(struct mqtt_response_publish *p) {
	const char *topic = p->topic_name;
	uint16_t tsize = p->topic_name_size;

	uint64_t mac = get_mac(topic, tsize);
	char *message = make_string(p->application_message, p->application_message_size);
	xlog("MQTT network 0x%lx %s", mac, message);
	free(message);

	// switch HOFLICHT on if darkness and handy logs into wlan
	if (mac == MAC_HANDY)
		if (sensor->lumi < DARKNESS)
			tasmota_power(HOFLICHT, 0, 1);

	return 0;
}

static int dispatch_tasmota(struct mqtt_response_publish *p) {
	tasmota_dispatch(p->topic_name, p->topic_name_size, p->application_message, p->application_message_size);
	return 0;
}

static int dispatch_sensor(struct mqtt_response_publish *p) {
	// dummy dispatcher for picam sensors
	return 0;
}

static int dispatch_solar(struct mqtt_response_publish *p) {
#ifdef SOLAR
	solar_dispatch(p->topic_name, p->topic_name_size, p->application_message, p->application_message_size);
#endif
	return 0;
}

static int dispatch(struct mqtt_response_publish *p) {
	// dump("MQTT", p);

	// notifications
	if (starts_with(TOPIC_NOTIFICATION, p->topic_name, p->topic_name_size))
		return dispatch_notification(p);

	// network
	if (starts_with(TOPIC_NETWORK, p->topic_name, p->topic_name_size))
		return dispatch_network(p);

	// sensor
	if (starts_with(TOPIC_SENSOR, p->topic_name, p->topic_name_size))
		return dispatch_sensor(p);

	// sensor
	if (starts_with(TOPIC_SOLAR, p->topic_name, p->topic_name_size))
		return dispatch_solar(p);

	// tasmota TASMOTA
	if (starts_with(TOPIC_TASMOTA, p->topic_name, p->topic_name_size))
		return dispatch_tasmota(p);

	// tasmota TELE
	if (starts_with(TOPIC_TELE, p->topic_name, p->topic_name_size))
		return dispatch_tasmota(p);

	// tasmota CMND
	if (starts_with(TOPIC_CMND, p->topic_name, p->topic_name_size))
		return dispatch_tasmota(p);

	// tasmota STAT
	if (starts_with(TOPIC_STAT, p->topic_name, p->topic_name_size))
		return dispatch_tasmota(p);

	// TODO tasmota/discovery

	dump("MQTT no dispatcher for message", p);

	return 0;
}

static void callback(void **unused, struct mqtt_response_publish *p) {
	dispatch(p);
}

static void loop() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("MQTT Error setting pthread_setcancelstate");
		return;
	}

	while (1) {
		mqtt_sync(client);
		msleep(500);
	}
}

static int init() {
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
	char client_id[128];
	snprintf(client_id, 128, "%s-mcp-rx", mcp->hostname);

	client = malloc(sizeof(*client));
	ZEROP(client);
	client->keep_alive = 30;

	fd = open_nb_socket(MQTT_HOST, MQTT_PORT);
	if (fd == -1)
		return xerr("MQTT Failed to open socket: ");

	if (mqtt_init(client, fd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), callback) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (mqtt_connect(client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (client->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (mqtt_subscribe(client, TOPIC_NOTIFICATION, 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (mqtt_subscribe(client, TOPIC_SENSOR"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (mqtt_subscribe(client, TOPIC_NETWORK"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (mqtt_subscribe(client, TOPIC_SOLAR"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (mqtt_subscribe(client, TOPIC_TASMOTA"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (mqtt_subscribe(client, TOPIC_TELE"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (mqtt_subscribe(client, TOPIC_CMND"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (mqtt_subscribe(client, TOPIC_STAT"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	mqtt_sync(client);
	return 0;
}

static void stop() {
	if (fd > 0)
		close(fd);
}

MCP_REGISTER(mqtt_rx, 2, &init, &stop, &loop);
