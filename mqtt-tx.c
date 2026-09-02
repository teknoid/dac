/*****************************************************************************

 mqtt publisher based on simple_publisher.c
 https://github.com/LiamBindle/MQTT-C

 ****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include <posix_sockets.h>
#include <mqttc.h>

#include "utils.h"
#include "mqtt.h"
#include "mcp.h"

#ifndef MQTT_HOST
#define	MQTT_HOST				"localhost"
#endif

#ifndef MQTT_PORT
#define MQTT_PORT				"1883"
#endif

#define TEMPLATE_NOTIFICATION	"{\"title\":\"%s\", \"text\":\"%s\", \"sound\":\"%s\"}"

static int fd;
static struct mqtt_client *client = NULL;
static uint8_t sendbuf[4096];
static uint8_t recvbuf[1024];

int publish(const char *topic, const char *message, int retain) {
	int rc = 0;

	// xlog("MQTT publish topic('%s') = %s", topic, message);

	if (client == NULL)
		return xerr("MQTT publish(): client not ready yet, check module registration priority");

	/* check that we don't have any errors */
	if (client->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	uint8_t flags = MQTT_PUBLISH_QOS_0;
	if (retain)
		flags |= MQTT_PUBLISH_RETAIN;

	if (message)
		rc = mqtt_publish(client, topic, message, strlen(message), flags);
	else
		rc = mqtt_publish(client, topic, "", 0, flags);

	if (rc != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	return mqtt_sync(client);
}

int mqtt_notify(const char *title, const char *text, const char *sound) {
	char message[0xff];
	snprintf(message, 0xff, TEMPLATE_NOTIFICATION, title, text, sound);
	return publish(TOPIC_NOTIFICATION, message, 0);
}

static int init() {
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;
	char client_id[128];
	snprintf(client_id, 128, "%s-mcp-tx", mcp->hostname);

	client = malloc(sizeof(*client));
	ZEROP(client);
	client->keep_alive = 30;

	// create new socket
	fd = open_nb_socket(MQTT_HOST, MQTT_PORT);
	if (fd == -1)
		return xerr("MQTT Failed to open socket: ");

	if (mqtt_init(client, fd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf), NULL) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (mqtt_connect(client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400) != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	if (client->error != MQTT_OK)
		return xerr("MQTT %s\n", mqtt_error_str(client->error));

	return 0;
}

static void stop() {
	if (fd > 0)
		close(fd);
}

MCP_REGISTER(mqtt_tx, 3, &init, &stop, NULL);
