/*****************************************************************************

 based on simple_publisher.c simple_subscriber.c
 https://github.com/LiamBindle/MQTT-C

 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <posix_sockets.h>
#include <mqttc.h>

#include "tasmota.h"
#include "frozen.h"
#include "utils.h"
#include "mqtt.h"
#include "lcd.h"
#include "mcp.h"

//
// MQTT-C's client is MUTEX'd - so we need two clients for simultaneous publish during subscribe callback
//
static int mqttfd_tx;
static struct mqtt_client *client_tx;
static uint8_t sendbuf_tx[4096];
static uint8_t recvbuf_tx[1024];

static int mqttfd_rx;
static struct mqtt_client *client_rx;
static uint8_t sendbuf_rx[4096];
static uint8_t recvbuf_rx[1024];

static int ready = 0;

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

// show on LCD display line 1 and 2
static void lcd(const char *line1, const char *line2) {
	if (!mcp->notifications_lcd)
		return;

#ifdef LCD
	lcd_print(line1, line2);
#endif
}

// desktop notifications via DBUS
static void desktop(const char *title, const char *text) {
	if (!mcp->notifications_desktop)
		return;

	size_t size = strlen(title) + strlen(text) + 256;
	char *command = (char*) malloc(size);
	snprintf(command, size, "%s %s \"%s\" \"%s\"", DBUS, NOTIFY_SEND, title, text);
	xlog("MQTT system: %s", command);
	system(command);
	free(command);
}

// play sound
static void play(const char *sound) {
	if (!mcp->notifications_sound)
		return;

	char *command = (char*) malloc(128);
	if (sound == NULL)
		snprintf(command, 128, "/usr/bin/aplay %s \"%s/mau.wav\"", APLAY_OPTIONS, APLAY_DIRECTORY);
	else
		snprintf(command, 128, "/usr/bin/aplay %s \"%s/%s\"", APLAY_OPTIONS, APLAY_DIRECTORY, sound);
	xlog("MQTT system: %s", command);
	system(command);
	free(command);
}

static int dispatch_notification(struct mqtt_response_publish *p) {
	const char *message = p->application_message;
	size_t msize = p->application_message_size;

	char *title = NULL, *text = NULL, *sound = NULL;
	json_scanf(message, msize, "{title: %Q, text: %Q, sound: %Q}", &title, &text, &sound);

	notify(title, text, sound);

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
#ifdef TASMOTA
	if (mac == MAC_HANDY)
		if (sensors->bh1750_lux < DARKNESS)
			tasmota_power(HOFLICHT, 0, 1);
#endif

	return 0;
}

static int dispatch_tasmota(struct mqtt_response_publish *p) {
#ifdef TASMOTA
	tasmota_dispatch(p->topic_name, p->topic_name_size, p->application_message, p->application_message_size);
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

	// tasmota TELE
	if (starts_with(TOPIC_TELE, p->topic_name, p->topic_name_size))
		return dispatch_tasmota(p);

	// tasmota CMND
	if (starts_with(TOPIC_CMND, p->topic_name, p->topic_name_size))
		return dispatch_tasmota(p);

	// tasmota STAT
	if (starts_with(TOPIC_STAT, p->topic_name, p->topic_name_size))
		return dispatch_tasmota(p);

	dump("MQTT no dispatcher for message", p);

	return 0;
}

static void callback(void **unused, struct mqtt_response_publish *p) {
	dispatch(p);
}

static void mqtt() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("MQTT Error setting pthread_setcancelstate");
		return;
	}

	while (1) {
		mqtt_sync(client_rx);
		mqtt_sync(client_tx);
		msleep(100);
	}
}

static int init() {
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;

	// publisher client
	client_tx = malloc(sizeof(*client_tx));
	ZERO(client_tx);

	mqttfd_tx = open_nb_socket(HOST, PORT);
	if (mqttfd_tx == -1)
		return xerr("MQTT Failed to open socket: ");

	if (mqtt_init(client_tx, mqttfd_tx, sendbuf_tx, sizeof(sendbuf_tx), recvbuf_tx, sizeof(recvbuf_tx), NULL) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	if (mqtt_connect(client_tx, CLIENT_ID"-tx", NULL, NULL, 0, NULL, NULL, connect_flags, 400) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	if (client_tx->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	// subscriber client
	client_rx = malloc(sizeof(*client_rx));
	ZERO(client_rx);

	mqttfd_rx = open_nb_socket(HOST, PORT);
	if (mqttfd_rx == -1)
		return xerr("MQTT Failed to open socket: ");

	if (mqtt_init(client_rx, mqttfd_rx, sendbuf_rx, sizeof(sendbuf_rx), recvbuf_rx, sizeof(recvbuf_rx), callback) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_connect(client_rx, CLIENT_ID"-rx", NULL, NULL, 0, NULL, NULL, connect_flags, 400) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (client_rx->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_NOTIFICATION, 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_SENSOR"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_NETWORK"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_TELE"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_CMND"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_STAT"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	ready = 1;
	return 0;
}

static void stop() {
	if (mqttfd_tx > 0)
		close(mqttfd_tx);

	if (mqttfd_rx > 0)
		close(mqttfd_rx);
}

int notify(const char *title, const char *text, const char *sound) {
	xdebug("MQTT notification %s/%s/%s", title, text, sound);

	lcd(title, text);
	desktop(title, text);

	if (sound != NULL)
		play(sound);

	return 0;
}

int publish(const char *topic, const char *message) {
	if (!ready)
		return xerr("MQTT publish(): client not ready yet, check module registration priority");

	// xlog("MQTT publish topic('%s') = %s", topic, message);

	/* check that we don't have any errors */
	if (client_tx->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	if (mqtt_publish(client_tx, topic, message, strlen(message), MQTT_PUBLISH_QOS_0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	return 0;
}

MCP_REGISTER(mqtt, 5, &init, &stop, &mqtt);
