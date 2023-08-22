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
#include <pthread.h>

#include <posix_sockets.h>
#include <mqttc.h>

#include "flamingo.h"
#include "tasmota.h"
#include "frozen.h"
#include "utils.h"
#include "mqtt.h"
#include "xmas.h"
#include "lcd.h"
#include "mcp.h"

#define MEAN	10

static pthread_t thread;

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

static unsigned int bh1750_lux_mean[MEAN];
static int mean;

static int ready = 0;

static int notifications = 1;

int publish(const char *topic, const char *message) {
	if (!ready)
		return xerr("MQTT publish(): client not ready yet, check module registration priority");

	xlog("MQTT publish topic('%s') = %s", topic, message);

	/* check that we don't have any errors */
	if (client_tx->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	if (mqtt_publish(client_tx, topic, message, strlen(message), MQTT_PUBLISH_QOS_0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	return 0;
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

// create null-terminated topic
static char* topic_string(struct mqtt_response_publish *p) {
	const char *topic = p->topic_name;
	uint16_t tsize = p->topic_name_size;

	char *t = (char*) malloc(tsize + 1);
	memcpy(t, topic, tsize);
	t[tsize] = '\0';
	return t;
}

// create null-terminated message
static char* message_string(struct mqtt_response_publish *p) {
	const char *message = p->application_message;
	size_t msize = p->application_message_size;

	char *m = (char*) malloc(msize + 1);
	memcpy(m, message, msize);
	m[msize] = '\0';
	return m;
}

// play sound
static void play(char *sound) {
	char *command = (char*) malloc(128);
	if (sound == NULL)
		snprintf(command, 128, "/usr/bin/aplay %s \"%s/mau.wav\"", APLAY_OPTIONS, APLAY_DIRECTORY);
	else
		snprintf(command, 128, "/usr/bin/aplay %s \"%s/%s\"", APLAY_OPTIONS, APLAY_DIRECTORY, sound);
	xlog("MQTT system: %s", command);
	system(command);
	free(command);
}

// special notification doorbell
static void doorbell() {
	xlog("MQTT doorbell");

	// show on LCD display line 1 and 2
	lcd_print("Ding", "Dong");

	// show desktop notification
//	char *command = (char*) malloc(256);
//	snprintf(command, 256, "%s %s \"%s\" \"%s\"", DBUS, NOTIFY_SEND, "Ding", "Dong");
//	xlog("MQTT system: %s", command);
//	system(command);
//	free(command);

	// play sound
	play("ding-dong.wav");
}

// decode flamingo message
static void flamingo(unsigned int code) {
	unsigned int xmitter;
	unsigned char command, channel, payload, rolling;

	xlog("MQTT flamingo");
	flamingo28_decode(code, &xmitter, &command, &channel, &payload, &rolling);

	switch (xmitter) {
	case 0x835a:
		switch (channel) {
		case 1:
			if (command == 2)
				xmas_on();
			else if (command == 0)
				xmas_off();
			break;
		}
		break;
	}
}

static int dispatch_notification(struct mqtt_response_publish *p) {
	const char *message = p->application_message;
	size_t msize = p->application_message_size;

	char *title = NULL, *text = NULL, *sound = NULL;
	json_scanf(message, msize, "{title: %Q, text: %Q, sound: %Q}", &title, &text, &sound);

	// show on LCD display line 1 and 2
	lcd_print(title, text);

	// show desktop notification
//	size_t size = strlen(title) + strlen(text) + 256;
//	char *command = (char*) malloc(size);
//	snprintf(command, size, "%s %s \"%s\" \"%s\"", DBUS, NOTIFY_SEND, title, text);
//	xlog("MQTT system: %s", command);
//	system(command);
//	free(command);

	// play sound
	play(sound);

	// release memory
	free(title);
	free(text);
	free(sound);

	return 0;
}

static void bh1750_calc_mean() {
	bh1750_lux_mean[mean++] = sensors->bh1750_lux;
	if (mean == MEAN)
		mean = 0;

	unsigned long sum = 0;
	for (int i = 0; i < MEAN; i++)
		sum += bh1750_lux_mean[i];

	sensors->bh1750_lux_mean = sum / MEAN;
}

static int dispatch_sensor(struct mqtt_response_publish *p) {
	const char *message = p->application_message;
	size_t msize = p->application_message_size;

	char *bh1750 = NULL;
	char *bmp280 = NULL;
	char *rf = NULL;

	json_scanf(message, msize, "{BH1750:%Q, BMP280:%Q, RfReceived:%Q}", &bh1750, &bmp280, &rf);

	if (bh1750 != NULL) {
		json_scanf(bh1750, strlen(bh1750), "{Illuminance:%d}", &sensors->bh1750_lux);
		free(bh1750);
		bh1750_calc_mean();
	}

	if (bmp280 != NULL) {
		json_scanf(bmp280, strlen(bmp280), "{Temperature:%f, Pressure:%f}", &sensors->bmp280_temp, &sensors->bmp280_baro);
		free(bmp280);
	}

	if (rf != NULL) {
		unsigned int rf_code;
		int bits;
		json_scanf(rf, strlen(rf), "{Data:%x, Bits:%d}", &rf_code, &bits);
		if (rf_code == DOORBELL)
			doorbell();
		else if (bits == 28)
			flamingo(rf_code);
		free(rf);
	}

//	xlog("MQTT BMP280 %.1f °C, %.1f hPa", sensors->bmp280_temp, sensors->bmp280_baro);

//	xlog("MQTT BH1750 %d lux", sensors->bh1750_lux);
//	xlog("MQTT BH1750 %d lux mean", sensors->bh1750_lux_mean);

	return 0;
}

static int dispatch_network(struct mqtt_response_publish *p) {
	const char *topic = p->topic_name;
	uint16_t tsize = p->topic_name_size;

	uint64_t mac = get_mac(topic, tsize);
	char *message = message_string(p);
	xlog("MQTT network 0x%lx %s", mac, message);
	free(message);

	// switch HOFLICHT on if darkness and handy logs into wlan
	if (mac == MAC_HANDY)
		if (sensors->bh1750_lux < DARKNESS)
			tasmota_power(HOFLICHT, 0, 1);

	return 0;
}

static int dispatch_tasmota(struct mqtt_response_publish *p) {
	char *topic = topic_string(p);
	char *message = message_string(p);
	xlog("MQTT tasmota topic('%s') = %s", topic, message);
	free(topic);
	free(message);

	tasmota_dispatch(p->topic_name, p->topic_name_size, p->application_message, p->application_message_size);

	return 0;
}

static int dispatch(struct mqtt_response_publish *p) {

	// notifications
	if (starts_with(NOTIFICATION, p->topic_name, p->topic_name_size))
		return dispatch_notification(p);

	// sensors
	if (starts_with(SENSOR, p->topic_name, p->topic_name_size))
		return dispatch_sensor(p);

	// network
	if (starts_with(NETWORK, p->topic_name, p->topic_name_size))
		return dispatch_network(p);

	// tasmotas
	if (starts_with(TASMOTA, p->topic_name, p->topic_name_size))
		return dispatch_tasmota(p);

	char *t = topic_string(p);
	char *m = message_string(p);
	xlog("MQTT no dispatcher for topic('%s'): %s", t, m);
	free(t);
	free(m);

	return 0;
}

static void callback(void **unused, struct mqtt_response_publish *p) {
	dispatch(p);
}

static void* mqtt(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("MQTT Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		mqtt_sync(client_rx);
		mqtt_sync(client_tx);
		msleep(100);
	}
}

static int init() {
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;

	// clear average value buffer
	ZERO(bh1750_lux_mean);
	mean = 0;

	// initialize sensor data
	sensors = malloc(sizeof(*sensors));
	ZERO(sensors);
	sensors->bh1750_lux = UINT16_MAX;
	sensors->bh1750_lux_mean = UINT16_MAX;
	sensors->bmp085_temp = UINT16_MAX;
	sensors->bmp085_baro = UINT16_MAX;
	sensors->bmp280_temp = UINT16_MAX;
	sensors->bmp280_baro = UINT16_MAX;

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

	if (mqtt_subscribe(client_rx, NOTIFICATION, 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, SENSOR"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, NETWORK"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (mqtt_subscribe(client_rx, TASMOTA"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_rx->error));

	if (pthread_create(&thread, NULL, &mqtt, NULL))
		return xerr("MQTT Error creating thread");

	ready = 1;
	xlog("MQTT initialized");
	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("MQTT Error canceling thread");

	if (pthread_join(thread, NULL))
		xlog("MQTT Error joining thread");

	if (mqttfd_tx > 0)
		close(mqttfd_tx);

	if (mqttfd_rx > 0)
		close(mqttfd_rx);
}

MCP_REGISTER(mqtt, 5, &init, &stop);
