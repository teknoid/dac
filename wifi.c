// gcc -DWIFI_MAIN -I./include -o wifi mcp.c utils.c wifi.c

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "utils.h"
#include "wifi.h"
#include "mqtt.h"
#include "mcp.h"

#define PORT						6666

#define BROADCAST					0xffffffffffff
#define THREETHREE					0x333300000000
#define THREETHREE_MASK				0xffff00000000

#define SECONDS_1D 					60 * 60 * 24
#define SECONDS_1H 					60 * 60
#define SECONDS_15M					60 * 15
#define SECONDS_3M					60 * 3

static station_t stations[STATIONS];
static station_t *zombies = &stations[STATIONS - 1];

static int server_fd;
static pthread_t listener_thread;

static time_t now_ts;

static int dump_line;

static station_t* station(uint64_t mac, char *ssid) {
	if (mac == 0)
		return 0;

	station_t *s = 0;
	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac == mac) {
			s = &stations[i];
			break;
		}

	if (s == 0)
		return 0;

	s->count++;
	s->ts = now_ts;
	if (ssid && strlen(ssid) > strlen(s->ssid))
		strcpy(s->ssid, ssid);

	return s;
}

static station_t* new_station(uint64_t mac, char *ssid) {
	if (mac == 0)
		return 0;

	if (mac == BROADCAST)
		return zombies;

	station_t *s = 0;
	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac == 0) {
			s = &stations[i];
			break;
		}

	if (s == 0) {
		xerr("WIFI station table is full!");
		return 0;
	}

	ZEROP(s);
	s->mac = mac;
	s->count++;
	s->ts = now_ts;
	uint642mac(mac, s->smac);
	uint642oui(mac, s->oui);
	strcpy(s->ssid, ssid);
	return s;
}

static client_t* client(station_t *s, uint64_t mac, char *ssid, char tag) {
	if (mac == 0)
		return 0;

	client_t *c = 0;
	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac == mac) {
			c = &(s->clients[i]);
			break;
		}

	if (c == 0)
		return 0;

	c->count++;
	c->ts = now_ts;
	c->tag = tag;
	if (ssid && strlen(ssid) > strlen(c->ssid))
		strcpy(c->ssid, ssid);

	return c;
}

static client_t* new_client(station_t *s, uint64_t mac, char *ssid, char tag) {
	if (mac == 0 || mac == BROADCAST)
		return 0;

	if ((mac & THREETHREE_MASK) == THREETHREE)
		return 0;

	if (mac == s->mac)
		return 0;

	client_t *c = 0;
	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac == 0) {
			c = &(s->clients[i]);
			break;
		}

	if (c == 0) {
		xerr("WIFI station %s client table is full!", s->smac);
		return 0;
	}

	ZEROP(c);
	c->mac = mac;
	c->count++;
	c->ts = now_ts;
	c->tag = tag;
	uint642mac(mac, c->smac);
	uint642oui(mac, c->oui);
	if (ssid && strlen(ssid) > 0)
		strcpy(c->ssid, ssid);

	return c;
}

static void assigned(station_t *s, uint64_t mac, char *ssid, char tag) {
	client_t *c = client(s, mac, ssid, tag);
	if (c)
		return;

	c = new_client(s, mac, ssid, tag);
	if (!c)
		return;

	xlog("WIFI new client %s assigned to %s", c->smac, *s->ssid != 0 ? s->ssid : s->smac);
	dump_line = 1;
}

static void unassigned(uint64_t mac, char *ssid, char tag) {
	for (int i = 0; i < STATIONS; i++) {
		station_t *s = &stations[i];
		client_t *c = client(s, mac, ssid, tag);
		if (c)
			return;
	}

	client_t *z = new_client(zombies, mac, ssid, tag);
	if (!z)
		return;

	if (ssid && strlen(ssid) > 0) {
		xlog("WIFI new client %s assigned to zombies hunting for %s", z->smac, ssid);
//		notify("New Zombie", ssid, "au.wav");
	} else
		xlog("WIFI new client %s assigned to zombies", z->smac);
	dump_line = 1;
}

static void cleanup() {
	for (int i = 0; i < CLIENTS; i++) {
		client_t *z = &(zombies->clients[i]);
		if (!z->mac)
			continue;

		for (int j = 0; j < STATIONS; j++) {
			station_t *s = &stations[j];
			if (!s->mac || s == zombies)
				continue;

			for (int k = 0; k < CLIENTS; k++) {
				client_t *c = &(s->clients[k]);
				if (!c->mac)
					continue;

				if (z->mac == c->mac) {
					xlog("WIFI zombie %s is assigned to %s -> removing", z->smac, *s->ssid != 0 ? s->ssid : s->smac);
					z->mac = 0;
					break;
				}
			}
		}
	}
}

static void expired() {
	for (int i = 0; i < STATIONS; i++) {
		station_t *s = &stations[i];
		if (!s->mac)
			continue;

		int sc = 0;
		for (int j = 0; j < CLIENTS; j++) {
			client_t *c = &(s->clients[j]);
			if (!c->mac)
				continue;

			// remove expired client
			int age = now_ts - c->ts;
			int e1 = c->ssid == 0 && c->count == 0 && age > SECONDS_3M;
			int e2 = c->ssid == 0 && c->count < 5 && age > SECONDS_15M;
			int e3 = c->count < 10 && age > SECONDS_1H;
			int e4 = age > SECONDS_1D;
			if (e1 || e2 || e3 || e4) {
				xlog("WIFI removing expired client %s from %s", c->smac, *s->ssid != 0 ? s->ssid : s->smac);
				ZEROP(c);
			} else
				sc++;
		}

		// remove expired station
		int age = now_ts - s->ts;
		int e1 = sc == 0 && s->count == 0 && age > SECONDS_1H;
		int e2 = sc == 0 && age > SECONDS_1D;
		if (e1 || e2) {
			xlog("WIFI removing expired station %", *s->ssid != 0 ? s->ssid : s->smac);
			ZEROP(s);
		}
	}
}

static void dump() {
	int sc = 0;
	int zc = 0;

	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac)
			sc++;

	for (int i = 0; i < CLIENTS; i++)
		if (zombies->clients[i].mac)
			zc++;

	xlog("\n\n### %d Stations ### %d Zombies ###", sc, zc);

#define HTEMPLATE "%-20s %-35s %6s %8s %10s %-35s"
#define STEMPLATE "\n%-20s %-35s %6d %8d %10d %-35s"
#define CTEMPLATE "%c %-18s %-35s %6d %8d %10d %-35s"

	xlog(HTEMPLATE, "MAC", "SSID", "Signal", "Age", "Count", "Hardware");
	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac) {
			station_t *s = &stations[i];
			xlog(STEMPLATE, s->smac, s->ssid, s->signal, now_ts - s->ts, s->count, s->oui);

			for (int i = 0; i < CLIENTS; i++)
				if (s->clients[i].mac) {
					client_t *c = &(s->clients[i]);
					xlog(CTEMPLATE, c->tag, c->smac, c->ssid, c->signal, now_ts - c->ts, c->count, c->oui);
				}
		}
}

static void parse(char *line, size_t len) {
	uint64_t bssid = 0, sa = 0, da = 0, ra = 0, ta = 0;
	char ssid[64];

	// split line into tokens
	ZERO(ssid);
	char *tokens = strdup(line);
	char *t = strtok(tokens, " ");
	while (t != NULL) {
		if (starts_with("BSSID", t, sizeof(t)))
			bssid = mac2uint64(t + 6);

		if (starts_with("DA", t, sizeof(t)))
			da = mac2uint64(t + 3);

		if (starts_with("SA", t, sizeof(t)))
			sa = mac2uint64(t + 3);

		if (starts_with("RA", t, sizeof(t)))
			ra = mac2uint64(t + 3);

		if (starts_with("TA", t, sizeof(t)))
			ta = mac2uint64(t + 3);

		if (!strcmp("Beacon", t) || !strcmp("Probe", t)) {
			char *x = strchr(line, '(');
			size_t y = strchr(x, ')') - x - 1;
			strncpy(ssid, x + 1, y);
		}

		t = strtok(NULL, " ");
	}

	// update or create station
	station_t *s = 0;
	if (bssid != 0) {
		s = station(bssid, ssid);
		if (!s)
			s = new_station(bssid, ssid);
	}

	if (s) {

		assigned(s, ra, ssid, 'r');
		assigned(s, ta, ssid, 't');
		assigned(s, da, ssid, 'd');
		assigned(s, sa, ssid, 's');

	} else {

		station_t *ras = station(ra, 0);
		if (ras) {
			assigned(ras, ta, ssid, 't');
			assigned(ras, da, ssid, 'd');
			assigned(ras, sa, ssid, 's');
			return;
		}

		station_t *tas = station(ta, 0);
		if (tas) {
			assigned(tas, ra, ssid, 'r');
			assigned(tas, da, ssid, 'd');
			assigned(tas, sa, ssid, 's');
			return;
		}

		station_t *sas = station(sa, 0);
		if (sas) {
			assigned(sas, ra, ssid, 'r');
			assigned(sas, ta, ssid, 't');
			assigned(sas, da, ssid, 'd');
			return;
		}

		station_t *das = station(da, 0);
		if (das) {
			assigned(das, ra, ssid, 'r');
			assigned(das, ta, ssid, 't');
			assigned(das, sa, ssid, 's');
			return;
		}

		unassigned(ra, ssid, 'r');
		unassigned(ta, ssid, 't');
		unassigned(da, ssid, 'd');
		unassigned(sa, ssid, 's');
	}
}

static void* listener(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	struct sockaddr_in address;
	socklen_t addrlen = sizeof(address);
	char line[1024];
	int client_fd;

	while (1) {
		if ((client_fd = accept(server_fd, (struct sockaddr*) &address, &addrlen)) < 0) {
			xerr("WIFI accept");
			return (void*) 0;
		}

		ssize_t numRead;
		size_t totRead;
		char *line_ptr;
		char ch;

		line_ptr = line;
		totRead = 0;
		while (1) {
			numRead = read(client_fd, &ch, 1);
			// xdebug("WIFI read %d %c 0x%02x", numRead, ch > 0x30 ? ch : ' ', ch);

			if (numRead <= 0)
				break;

			if (ch == '\n') {
				*line_ptr++ = '\0';
				dump_line = 0;
				parse(line, totRead);
				if (dump_line)
					xdebug(line);
				line_ptr = line;
				totRead = 0;
				continue;
			}

			totRead++;
			*line_ptr++ = ch;
		}

		close(client_fd);
	}
}

static void loop() {
	while (1) {
		sleep(1);
		now_ts = time(NULL);

		zombies->ts = now_ts;

		if (now_ts % 10 == 0)
			cleanup();

		if (now_ts % 30 == 0)
			expired();

		if (now_ts % 60 == 0)
			dump();
	}
}

static int init() {
	xlog("WIFI init");

	strcpy(zombies->ssid, "Zombies");
	zombies->mac = 0xaffeaffeaffe;
	uint642mac(zombies->mac, zombies->smac);

	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(PORT);

	// Creating socket file descriptor
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket failed");
		exit(EXIT_FAILURE);
	}

	int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

	if (bind(server_fd, (struct sockaddr*) &address, sizeof(address)) < 0) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	if (listen(server_fd, 3) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	// start listener thread
	if (pthread_create(&listener_thread, NULL, &listener, NULL))
		return xerr("Error creating thread_ra");

	xerr("WIFI listening");
	return 0;
}

static void stop() {
	if (pthread_cancel(listener_thread))
		xlog("Error canceling thread_ra");

	if (pthread_join(listener_thread, NULL))
		xlog("Error joining thread_ra");

	if (server_fd)
		close(server_fd);
}

static int test() {
	char mac1[] = "f0:fe:6b:27:a8:b2", mac2[20], mac3[128];
	uint64_t mac = mac2uint64(mac1);
	uint642mac(mac, mac2);
	uint642oui(mac, mac3);
	xlog("mac1=%s uint64=%lx mac2=%s mac3=%s", mac1, mac, mac2, mac3);
	return 0;
}

int wifi_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	int c;
	while ((c = getopt(argc, argv, "lt")) != -1) {
		switch (c) {
		case 'l':
			return mcp_main(argc, argv);
		case 't':
			return test();
		default:
			xlog("unknown getopt %c", c);
		}
	}

	return 0;
}

#ifdef WIFI_MAIN
int main(int argc, char **argv) {
	return wifi_main(argc, argv);
}
#endif

MCP_REGISTER(wifi, 15, &init, &stop, &loop);
