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
#include "utils.h"
#include "mqtt.h"
#include "lcd.h"
#include "mcp.h"

static int mqttfd;
static pthread_t thread;

static struct mqtt_client *client;
static uint8_t sendbuf[2048];
static uint8_t recvbuf[2048];
static int ready = 0;

sensors_t *sensors;

void shelly(unsigned int shelly, const char *cmd) {
	char *t = (char*) malloc(32);
	snprintf(t, 32, "shelly/%6X/cmnd/POWER", shelly);
	publish(t, cmd);
	xlog("MQTT switched shelly %6X %s", shelly, cmd);
}

int publish(const char *topic, const char *message) {
	if (!ready)
		return xerr("MQTT publish(): client not ready yet, check module registration priority");

	/* check that we don't have any errors */
	if (client->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	mqtt_publish(client, topic, message, strlen(message), MQTT_PUBLISH_QOS_0);
	return 0;
}

static int notification(const char *message, size_t msize) {
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

static int sensor(const char *message, size_t msize) {
	char *bh1750 = NULL;
	char *bmp280 = NULL;

	json_scanf(message, msize, "{BH1750:%Q, BMP280:%Q}", &bh1750, &bmp280);

	if (bh1750 != NULL)
		json_scanf(bh1750, strlen(bh1750), "{Illuminance:%d}", &sensors->bh1750_lux);

	if (bmp280 != NULL)
		json_scanf(bmp280, strlen(bmp280), "{Temperature:%f, Pressure:%f}", &sensors->bmp280_temp, &sensors->bmp280_baro);

	xlog("MQTT BH1750 %d lux", sensors->bh1750_lux);
	xlog("MQTT BMP280 %.1f °C, %.1f hPa", sensors->bmp280_temp, sensors->bmp280_baro);

	free(bh1750);
	free(bmp280);
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

static int dispatch(struct mqtt_response_publish *published) {
	const char *message = published->application_message;
	size_t msize = published->application_message_size;

	if (starts_with(NOTIFICATION, published->topic_name))
		return notification(message, msize);

	if (starts_with(SENSOR, published->topic_name))
		return sensor(message, msize);

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
		mqtt_sync(client);
		msleep(100);
	}
}

static int init() {
	client = malloc(sizeof(*client));
	memset(client, 0, sizeof(*client));

	sensors = malloc(sizeof(*sensors));
	memset(sensors, 0, sizeof(*sensors));

	// initialize sensor data
	sensors->bh1750_lux = INT_MAX;
	sensors->bmp085_temp = INT_MAX;
	sensors->bmp085_baro = INT_MAX;
	sensors->bmp280_temp = INT_MAX;
	sensors->bmp280_baro = INT_MAX;

	/* open the non-blocking TCP socket (connecting to the broker) */
	mqttfd = open_nb_socket(HOST, PORT);
	if (mqttfd == -1)
		return xerr("MQTT Failed to open socket: ");

	/* setup a client */
	if (mqtt_init(client, mqttfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), callback) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	/* Create an anonymous and clean session */
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;

	/* Send connection request to the broker. */
	if (mqtt_connect(client, CLIENT_ID, NULL, NULL, 0, NULL, NULL, connect_flags, 400) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	/* check that we don't have any errors */
	if (client->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	/* subscribe to topics */
	if (mqtt_subscribe(client, NOTIFICATION, 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (mqtt_subscribe(client, SENSOR"/#", 0) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

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

	if (mqttfd > 0)
		close(mqttfd);
}

MCP_REGISTER(mqtt, 5, &init, &stop);
