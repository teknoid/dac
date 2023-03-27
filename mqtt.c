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
#include <mqtt.h>

#include "frozen.h"
#include "utils.h"
#include "mqtt.h"
#include "lcd.h"
#include "mcp.h"

static int mqttfd;
static pthread_t thread;

static struct mqtt_client client;
static uint8_t sendbuf[2048];
static uint8_t recvbuf[1024];

static void publish_callback(void **unused, struct mqtt_response_publish *published) {
	/* note that published->topic_name is NOT null-terminated (here we'll change it to a c-string) */
	char *topic_name = (char*) malloc(published->topic_name_size + 1);
	memcpy(topic_name, published->topic_name, published->topic_name_size);
	topic_name[published->topic_name_size] = '\0';
	xlog("MQTT topic('%s'): %s\n", topic_name, (const char*) published->application_message);

	char *title = NULL, *text = NULL;
	json_scanf(published->application_message, strlen(published->application_message), "{title: %Q, text: %Q}", &title, &text);
	lcd_command(LCD_CLEAR);
	msleep(2);
	lcd_printlc(1, 1, title);
	lcd_printlc(2, 1, text);
	lcd_backlight_on();

	free(title);
	free(text);
	free(topic_name);
}

static void* mqtt(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		msleep(100);
		mqtt_sync(&client);
	}
}

static int init() {
	/* open the non-blocking TCP socket (connecting to the broker) */
	mqttfd = open_nb_socket(HOST, PORT);
	if (mqttfd == -1)
		return xerr("Failed to open socket: ");

	/* setup a client */
	mqtt_init(&client, mqttfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), publish_callback);

	/* Create an anonymous and clean session */
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;

	/* Send connection request to the broker. */
	mqtt_connect(&client, CLIENT_ID, NULL, NULL, 0, NULL, NULL, connect_flags, 400);

	/* check that we don't have any errors */
	if (client.error != MQTT_OK)
		return xerr("error: %s\n", mqtt_error_str(client.error));

	/* subscribe to topic */
	mqtt_subscribe(&client, TOPIC, 0);

	if (pthread_create(&thread, NULL, &mqtt, NULL))
		return xerr("Error creating mqtt thread");

	xlog("MQTT initialized");
	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("Error canceling mqtt thread");

	if (pthread_join(thread, NULL))
		xlog("Error joining mqtt thread");

	if (mqttfd > 0)
		close(mqttfd);
}

MCP_REGISTER(mqtt, 2, &init, &stop);
