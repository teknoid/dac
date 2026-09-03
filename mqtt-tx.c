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

static struct mqtt_client *client = NULL;
static uint8_t sendbuf[4096];
static uint8_t recvbuf[1024];

static void reconnect(struct mqtt_client *client, void **ptr) {
	/* Close the clients socket if this isn't the initial reconnect call */
	if (client->error != MQTT_ERROR_INITIAL_RECONNECT)
		close(client->socketfd);

	/* Perform error handling here. */
	if (client->error != MQTT_ERROR_INITIAL_RECONNECT)
		xerr("MQTT-TX reconnect_client: called while client was in error state \"%s\"\n", mqtt_error_str(client->error));

	/* Open a new socket. */
	int sockfd = open_nb_socket(MQTT_HOST, MQTT_PORT);
	if (sockfd == -1) {
		xerr("MQTT-TX Failed to open socket");
		return;
	}

	/* Reinitialize the client. */
	mqtt_reinit(client, sockfd, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf));

	char client_id[128];
	snprintf(client_id, 128, "%s-mcp-tx", mcp->hostname);

	/* Ensure we have a clean session */
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;

	/* Send connection request to the broker. */
	if (mqtt_connect(client, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 400) != MQTT_OK)
		xerr("MQTT-TX %s\n", mqtt_error_str(client->error));
}

static void callback(void **unused, struct mqtt_response_publish *p) {
}

int publish(const char *topic, const char *message, int retain) {
	int rc = 0;

	// xlog("MQTT-TX publish topic('%s') = %s", topic, message);

	if (client == NULL)
		return xerr("MQTT-TX publish(): client not ready yet, check module registration priority");

	/* check that we don't have any errors */
	if (client->error != MQTT_OK)
		return xerr("MQTT-TX %s\n", mqtt_error_str(client->error));

	uint8_t flags = MQTT_PUBLISH_QOS_0;
	if (retain)
		flags |= MQTT_PUBLISH_RETAIN;

	if (message)
		rc = mqtt_publish(client, topic, message, strlen(message), flags);
	else
		rc = mqtt_publish(client, topic, "", 0, flags);

	if (rc != MQTT_OK)
		return xerr("MQTT-TX %s\n", mqtt_error_str(client->error));

	return mqtt_sync(client);
}

int mqtt_notify(const char *title, const char *text, const char *sound) {
	char message[0xff];
	snprintf(message, 0xff, TEMPLATE_NOTIFICATION, title, text, sound);
	return publish(TOPIC_NOTIFICATION, message, 0);
}

static int init() {
	client = malloc(sizeof(*client));
	ZEROP(client);

	client->keep_alive = 30;

	mqtt_init_reconnect(client, reconnect, NULL, callback);
	mqtt_sync(client);

	return 0;
}

static void stop() {
	if (client != NULL && client->socketfd)
		close(client->socketfd);
}

MCP_REGISTER(mqtt_tx, 3, &init, &stop, NULL);
