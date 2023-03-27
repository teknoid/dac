/*****************************************************************************

 based on simple_subscriber.c
 https://github.com/LiamBindle/MQTT-C

 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
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

static struct mqtt_client *client = NULL;
static uint8_t sendbuf[2048];
static uint8_t recvbuf[2048];

sensors_t *sensors;

int publish(const char *topic, const char *message) {
	if (client == NULL)
		return xerr("MQTT client is NULL, please check module registration priority!");

	/* check that we don't have any errors */
	if (client->error != MQTT_OK)
		return xerr("MQTT error: %s\n", mqtt_error_str(client->error));

	mqtt_publish(client, topic, message, strlen(message), MQTT_PUBLISH_QOS_0);

	return 0;
}

static void notification(const char *message) {
	char *title = NULL, *text = NULL;
	json_scanf(message, strlen(message), "{title: %Q, text: %Q}", &title, &text);

#ifdef LCD
	lcd_command(LCD_CLEAR);
	msleep(2);
	lcd_printlc(1, 1, title);
	lcd_printlc(2, 1, text);
	lcd_backlight_on();
#endif

	free(title);
	free(text);
}

static void sensor(const char *message) {
	char *title = NULL, *text = NULL;
	json_scanf(message, strlen(message), "{title: %Q, text: %Q}", &title, &text);

	free(title);
	free(text);
}

static void publish_callback(void **unused, struct mqtt_response_publish *published) {
	// xlog("MQTT topic('%s'): %s\n", published->topic_name, published->application_message);

	xlog("MQTT callback");

	if (starts_with(NOTIFICATION, published->topic_name))
		notification(published->application_message);

	if (starts_with(SENSOR, published->topic_name))
		sensor(published->application_message);
}

static void* mqtt(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		msleep(100);
		mqtt_sync(client);
	}
}

static int init() {
	client = malloc(sizeof(*client));
	memset(client, 0, sizeof(*client));

	sensors = malloc(sizeof(*sensors));
	memset(sensors, 0, sizeof(*sensors));

	// initialize sensor data
	sensors->bh1750_lux = -1;
	sensors->bmp085_temp = -1;
	sensors->bmp085_baro = -1;
	sensors->bmp280_temp = -1;
	sensors->bmp280_baro = -1;

	/* open the non-blocking TCP socket (connecting to the broker) */
	mqttfd = open_nb_socket(HOST, PORT);
	if (mqttfd == -1)
		return xerr("MQTT Failed to open socket: ");

	/* setup a client */
	mqtt_init(client, mqttfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);

	/* Create an anonymous and clean session */
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;

	/* Send connection request to the broker. */
	mqtt_connect(client, CLIENT_ID, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

	/* check that we don't have any errors */
	if (client->error != MQTT_OK)
		return xerr("MQTT error: %s\n", mqtt_error_str(client->error));

	/* subscribe to topics */
	mqtt_subscribe(client, NOTIFICATION, 0);
	mqtt_subscribe(client, SENSOR, 0);

	if (pthread_create(&thread, NULL, &mqtt, NULL))
		return xerr("MQTT Error creating thread");

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
