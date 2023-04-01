/*****************************************************************************

 based on simple_subscriber.c
 https://github.com/LiamBindle/MQTT-C

 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>

#include <posix_sockets.h>
#include <mqttc.h>

#include "frozen.h"
#include "shelly.h"
#include "utils.h"
#include "mqtt.h"
#include "lcd.h"
#include "mcp.h"

static pthread_t thread;

//
// MQTT-C's client is MUTEX'd - so we need two clients for simultaneous publish during subscribe callback
//
static int mqttfd_tx;
static struct mqtt_client *client_tx;
static uint8_t sendbuf_tx[1024];
static uint8_t recvbuf_tx[1024];

static int mqttfd_rx;
static struct mqtt_client *client_rx;
static uint8_t sendbuf_rx[4096];
static uint8_t recvbuf_rx[1024];

static int ready = 0;

sensors_t *sensors;

int publish(const char *topic, const char *message) {
	if (!ready)
		return xerr("MQTT publish(): client not ready yet, check module registration priority");

	xlog("topic :: %s || message :: %s", topic, message);

	/* check that we don't have any errors */
	if (client_tx->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	if (mqtt_publish(client_tx, topic, message, strlen(message), MQTT_PUBLISH_QOS_0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client_tx->error));

	return 0;
}

// create null-terminated topic
static char* topic_string(struct mqtt_response_publish *published) {
	const char *topic = published->topic_name;
	uint16_t tsize = published->topic_name_size;

	char *t = (char*) malloc(tsize + 1);
	memcpy(t, topic, tsize);
	t[tsize] = '\0';
	return t;
}

// create null-terminated message
static char* message_string(struct mqtt_response_publish *published) {
	const char *message = published->application_message;
	size_t msize = published->application_message_size;

	char *m = (char*) malloc(msize + 1);
	memcpy(m, message, msize);
	m[msize] = '\0';
	return m;
}

static int dispatch_notification(const char *message, size_t msize) {
	char *title = NULL, *text = NULL;
	json_scanf(message, msize, "{title: %Q, text: %Q}", &title, &text);

	// show on LCD display line 1 and 2
	lcd_print(title, text);

	// show desktop notification
	size_t size = strlen(title) + strlen(text) + 256;
	char *command = (char*) malloc(size);
	snprintf(command, size, "%s %s \"%s\" \"%s\"", DBUS, NOTIFYSEND, title, text);
	system(command);
	xlog("system: %s", command);

	// play sound
	system("/usr/bin/aplay -q -D hw:CARD=Device_1 /home/hje/mau.wav");

	free(title);
	free(text);
	return 0;
}

static int dispatch_sensor(const char *message, size_t msize) {
	char *bh1750 = NULL;
	char *bmp280 = NULL;

	json_scanf(message, msize, "{BH1750:%Q, BMP280:%Q}", &bh1750, &bmp280);

	if (bh1750 != NULL)
		json_scanf(bh1750, strlen(bh1750), "{Illuminance:%d}", &sensors->bh1750_lux);

	if (bmp280 != NULL)
		json_scanf(bmp280, strlen(bmp280), "{Temperature:%f, Pressure:%f}", &sensors->bmp280_temp, &sensors->bmp280_baro);

	// xlog("MQTT BH1750 %d lux", sensors->bh1750_lux);
	// xlog("MQTT BMP280 %.1f °C, %.1f hPa", sensors->bmp280_temp, sensors->bmp280_baro);

	free(bh1750);
	free(bmp280);
	return 0;
}

static int dispatch(struct mqtt_response_publish *published) {
	const char *topic = published->topic_name;
	uint16_t tsize = published->topic_name_size;

	const char *message = published->application_message;
	size_t msize = published->application_message_size;

	if (starts_with(NOTIFICATION, published->topic_name))
		return dispatch_notification(message, msize);

	if (starts_with(SENSOR, published->topic_name))
		return dispatch_sensor(message, msize);

	if (starts_with(SHELLY, published->topic_name))
		return shelly_dispatch(topic, tsize, message, msize);

	char *t = topic_string(published);
	char *m = message_string(published);
	xlog("MQTT no dispatcher for topic('%s'): %s", t, m);
	free(t);
	free(m);

	return 0;
}

static void callback(void **unused, struct mqtt_response_publish *published) {
	dispatch(published);
}

static void* mqtt(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("MQTT Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		mqtt_sync(client_tx);
		mqtt_sync(client_rx);
		msleep(100);
	}
}

static int init() {
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;

	// initialize sensor data
	sensors = malloc(sizeof(*sensors));
	memset(sensors, 0, sizeof(*sensors));
	sensors->bh1750_lux = INT_MAX;
	sensors->bmp085_temp = INT_MAX;
	sensors->bmp085_baro = INT_MAX;
	sensors->bmp280_temp = INT_MAX;
	sensors->bmp280_baro = INT_MAX;

	// publisher client
	client_tx = malloc(sizeof(*client_tx));
	memset(client_tx, 0, sizeof(*client_tx));

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
	memset(client_rx, 0, sizeof(*client_rx));

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

	if (mqtt_subscribe(client_rx, SHELLY"/#", 0) != MQTT_OK)
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
