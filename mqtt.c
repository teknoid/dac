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

#include "ledstrip.h"
#include "sensors.h"
#include "tasmota.h"
#include "frozen.h"
#include "solar.h"
#include "utils.h"
#include "mqtt.h"
#include "lcd.h"
#include "mcp.h"

#ifndef MQTT_HOST
#define	MQTT_HOST			"mqtt"
#endif

#ifndef MQTT_PORT
#define MQTT_PORT			"1883"
#endif

#define APLAY_OPTIONS		"-q -D hw:CARD=Device"
#define APLAY_DIRECTORY 	"/home/hje/sounds/16"

#define DBUS				"DISPLAY=:0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"
#define NOTIFY_SEND			"/usr/bin/notify-send -i /home/hje/Pictures/icons/mosquitto.png"

#define MAC_HANDY			0xfc539ea93ac5
#define DARKNESS			50

//
// MQTT-C's client is MUTEX'd - so we need two clients for simultaneous publish during subscribe callback
//
static int fd_tx;
static struct mqtt_client *client_tx;
static uint8_t sendbuf_tx[4096];
static uint8_t recvbuf_tx[1024];

static int fd_rx;
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

// ledstrip blink red
static void led() {
#ifdef LCD
	// TODO mcp->notifications_led
	ledstrip_blink_red();
#endif
}

// show on LCD display line 1 and 2
static void lcd(const char *line1, const char *line2) {
#ifdef LCD
	if (!mcp->notifications_lcd)
		return;

	lcd_print(line1, line2);
#endif
}

// desktop notifications via DBUS
static void desktop(const char *title, const char *text) {
#ifdef LCD
	if (!mcp->notifications_desktop)
		return;

	size_t size = strlen(title) + strlen(text) + 256;
	char *command = (char*) malloc(size);
	snprintf(command, size, "%s %s \"%s\" \"%s\"", DBUS, NOTIFY_SEND, title, text);
	xlog("MQTT system: %s", command);
	system(command);
	free(command);
#endif
}

// play sound
static void play(const char *sound) {
#ifdef MIXER
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
#endif
}

static int dispatch_notification(struct mqtt_response_publish *p) {
	const char *message = p->application_message;
	size_t msize = p->application_message_size;

	char *title = NULL, *text = NULL, *sound = NULL;
	json_scanf(message, msize, "{title:%Q, text:%Q, sound:%Q}", &title, &text, &sound);

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
	if (mac == MAC_HANDY)
		if (sensors->lumi < DARKNESS)
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
	char *idc = NULL, *cmd = NULL;
	int r = 0;

	json_scanf(p->application_message, p->application_message_size, "{id:%Q, r:%d, cmd:%Q}", &idc, &r, &cmd);
#ifdef SOLAR
	unsigned int id = (unsigned int) strtol(idc, NULL, 16);
	solar_toggle_id(id, r);
#endif

	free(idc);
	free(cmd);
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

	mqtt_sync(client_rx);
	msleep(100);
	mqtt_sync(client_tx);
	msleep(100);

	// Test
	notify("Test", "test", "mau4.wav");

	while (1) {
		mqtt_sync(client_rx);
		msleep(100);
		mqtt_sync(client_tx);
		msleep(100);
	}
}

// init publisher client
static int init_tx() {
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;

	char hostname[64], client_id[128];
	gethostname(hostname, 64);

	client_tx = malloc(sizeof(*client_tx));
	ZEROP(client_tx);

	snprintf(client_id, 128, "%s-mcp-tx", hostname);
	fd_tx = open_nb_socket(MQTT_HOST, MQTT_PORT);
	if (fd_tx == -1)
		return xerr("MQTT Failed to open socket: ");

	if (mqtt_init(client_tx, fd_tx, sendbuf_tx, sizeof(sendbuf_tx), recvbuf_tx, sizeof(recvbuf_tx), NULL) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	if (mqtt_connect(client_tx, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	if (client_tx->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	return 0;
}

// init subscriber client
static int init_rx() {
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;

	char hostname[64], client_id[128];
	gethostname(hostname, 64);

	client_rx = malloc(sizeof(*client_rx));
	ZEROP(client_rx);

	snprintf(client_id, 128, "%s-mcp-rx", hostname);
	fd_rx = open_nb_socket(MQTT_HOST, MQTT_PORT);
	if (fd_rx == -1)
		return xerr("MQTT Failed to open socket: ");

	if (mqtt_init(client_rx, fd_rx, sendbuf_rx, sizeof(sendbuf_rx), recvbuf_rx, sizeof(recvbuf_rx), callback) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_connect(client_rx, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (client_rx->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_NOTIFICATION, 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_SENSOR"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_NETWORK"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_SOLAR"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_TASMOTA"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_TELE"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_CMND"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TOPIC_STAT"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	return 0;
}

static int init() {
	if (init_tx())
		return -1;

	if (init_rx())
		return -1;

	ready = 1;
	return 0;
}

static void stop() {
	if (fd_tx > 0)
		close(fd_tx);

	if (fd_rx > 0)
		close(fd_rx);
}

int notify(const char *title, const char *text, const char *sound) {
	xdebug("MQTT notification %s/%s/%s", title, text, sound);

	lcd(title, text);
	desktop(title, text);
	if (sound != NULL)
		play(sound);

	return 0;
}

int notify_red(const char *title, const char *text, const char *sound) {
	xdebug("MQTT notification %s/%s/%s", title, text, sound);

	led();
	lcd(title, text);
	desktop(title, text);
	if (sound != NULL)
		play(sound);

	return 0;
}

int publish(const char *topic, const char *message, int retain) {
	int rc = 0;

	if (!ready)
		return xerr("MQTT publish(): client not ready yet, check module registration priority");

	// xlog("MQTT publish topic('%s') = %s", topic, message);

	/* check that we don't have any errors */
	if (client_tx->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	uint8_t flags = MQTT_PUBLISH_QOS_0;
	if (retain)
		flags |= MQTT_PUBLISH_RETAIN;

	if (message)
		rc = mqtt_publish(client_tx, topic, message, strlen(message), flags);
	else
		rc = mqtt_publish(client_tx, topic, "", 0, flags);

	if (rc != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	return 0;
}

int publish_oneshot(const char *topic, const char *message, int retain) {
	if (init())
		return -1;
	ready = 1;
	publish(topic, message, retain);
	return mqtt_sync(client_tx);
}

MCP_REGISTER(mqtt, 2, &init, &stop, &loop);
