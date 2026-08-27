// gcc -DWIFI_MAIN -I./include -o wifi mcp.c utils.c wifi.c

// iw phy phy1 interface add mon1 type monitor
// ifconfig mon1 up
// tcpdump -nevi mon1 | nc tron 6666

// while true; do for c in `seq 1 14`; do iwconfig mon1 channel $c; sleep 1s; done; done

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
#define SECONDS_1HX 				60 * 60 + 300
#define SECONDS_1H 					60 * 60
#define SECONDS_30M					60 * 30
#define SECONDS_10M					60 * 10

static station_t stations[STATIONS];
static station_t *zombies = &stations[STATIONS - 1];

static pthread_t listener_thread;
static time_t now_ts;
static int server_fd;

static unsigned int line_count;
static int dump_line;

static station_t* station(uint64_t mac, int create, char *ssid) {
	if (mac == 0 || mac == BROADCAST)
		return 0;

	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac == mac) {
			station_t *s = &stations[i];

			// station found
			s->count++;
			s->ts = now_ts;
			if (ssid && strlen(ssid) > 0)
				strcpy(s->ssid, ssid);

			return s;
		}

	// do not create new entry if not found
	if (!create)
		return 0;

	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac == 0) {
			station_t *s = &stations[i];

			// create new entry
			ZEROP(s);
			s->mac = mac;
			s->count++;
			s->ts = now_ts;
			uint642mac(mac, s->smac);
			uint642oui(mac, s->oui);
			if (ssid && strlen(ssid) > 0)
				strcpy(s->ssid, ssid);

			xlog("WIFI new station %s (%s)", s->smac, *s->ssid != 0 ? s->ssid : s->smac);
			dump_line = 1;

			return s;
		}

	xerr("WIFI stations table is full!");
	return 0;
}

static client_t* client(station_t *s, uint64_t mac, int create, char *ssid, char tag) {
	if (mac == 0 || mac == BROADCAST || mac == s->mac || (mac & THREETHREE_MASK) == THREETHREE)
		return 0;

	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac == mac) {
			client_t *c = &(s->clients[i]);

			// client found
			int age = now_ts - c->ts;
			if (age > SECONDS_1HX) {
				xlog("WIFI client %s station %s is back", c->smac, *s->ssid != 0 ? s->ssid : s->smac);
				// notify("client is back", c->smac, "au.wav");
			}

			c->count++;
			c->ts = now_ts;
			c->tag = tag;
			if (ssid && strlen(ssid) > 0)
				strcpy(c->ssid, ssid);

			return c;
		}

	// do not create new entry if not found
	if (!create)
		return 0;

	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac == 0) {
			client_t *z = &(s->clients[i]);

			// create new entry
			ZEROP(z);
			z->mac = mac;
			z->count++;
			z->ts = now_ts;
			z->tag = tag;
			uint642mac(mac, z->smac);
			uint642oui(mac, z->oui);
			if (ssid && strlen(ssid) > 0)
				strcpy(z->ssid, ssid);

			xlog("WIFI new client %s assigned to %s", z->smac, *s->ssid != 0 ? s->ssid : s->smac);
			dump_line = 1;

			return z;
		}

	xerr("WIFI station %s client table is full!", s->smac);
	return 0;
}

static client_t* client_unassigned(uint64_t mac, char *ssid, char tag) {
	if (mac == 0 || mac == BROADCAST)
		return 0;

	for (int i = 0; i < STATIONS; i++) {
		station_t *s = &stations[i];
		client_t *c = client(s, mac, 0, ssid, tag);
		if (c)
			return c; // found
	}

	// not found - create zombie
	client_t *c = client(zombies, mac, 1, ssid, 'z');
	return c;
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
	station_t *bss = station(bssid, 1, ssid);
	if (bss) {
		client(bss, ra, 1, ssid, 'r');
		client(bss, ta, 1, ssid, 't');
		client(bss, da, 1, ssid, 'd');
		client(bss, sa, 1, ssid, 's');
	}

	// assign to RA station
	station_t *ras = station(ra, 0, 0);
	if (ras) {
		client(ras, ta, 1, ssid, 't');
		client(ras, da, 1, ssid, 'd');
		client(ras, sa, 1, ssid, 's');
	}

	// assign to TA station
	station_t *tas = station(ta, 0, 0);
	if (tas) {
		client(tas, ra, 1, ssid, 'r');
		client(tas, da, 1, ssid, 'd');
		client(tas, sa, 1, ssid, 's');
	}

	// assign to SA station
	station_t *sas = station(sa, 0, 0);
	if (sas) {
		client(sas, ra, 1, ssid, 'r');
		client(sas, ta, 1, ssid, 't');
		client(sas, da, 1, ssid, 'd');
	}

	// assign to DA station
	station_t *das = station(da, 0, 0);
	if (das) {
		client(das, ra, 1, ssid, 'r');
		client(das, ta, 1, ssid, 't');
		client(das, sa, 1, ssid, 's');
	}

	// search client in all stations or create new zombie
	int assigned = bss || ras || tas || sas || das;
	if (!assigned) {
		client_unassigned(ra, ssid, 'r');
		client_unassigned(ta, ssid, 't');
		client_unassigned(da, ssid, 'd');
		client_unassigned(sa, ssid, 's');
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
				line_count++;
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

static void cleanup() {
	for (int i = 0; i < CLIENTS; i++) {
		client_t *z = &(zombies->clients[i]);
		if (!z->mac)
			continue;

		int assigned = 0;
		for (int j = 0; j < STATIONS; j++) {
			station_t *s = &stations[j];
			if (!s->mac || s == zombies)
				continue;

			if (z->mac == s->mac) {
				xlog("WIFI zombie %s is station %s -> removing", z->smac, *s->ssid != 0 ? s->ssid : s->smac);
				z->mac = 0;
				break;
			}

			for (int k = 0; k < CLIENTS; k++) {
				client_t *c = &(s->clients[k]);
				if (!c->mac)
					continue;

				if (z->mac == c->mac) {
					xlog("WIFI zombie %s assigned to %s -> removing", z->smac, *s->ssid != 0 ? s->ssid : s->smac);

					// take over ssid of probe request
					if (strlen(z->ssid) > 0)
						strcpy(c->ssid, z->ssid);

					assigned++;
					break;
				}
			}
		}

		// remove
		if (assigned)
			z->mac = 0;
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
			int e1 = c->count == 1 && age > SECONDS_10M;
			int e2 = c->count < 10 && age > SECONDS_30M;
			int e3 = c->count < 20 && age > SECONDS_1H;
			int e4 = age > SECONDS_1D;
			if (e1 || e2 || e3 || e4) {
				xlog("WIFI removing expired client %s from %s", c->smac, *s->ssid != 0 ? s->ssid : s->smac);
				ZEROP(c);
			} else
				sc++;
		}

		// remove expired station
		int age = now_ts - s->ts;
		int e1 = sc == 0 && s->count < 10 && age > SECONDS_1H;
		int e2 = sc == 0 && age > SECONDS_1D;
		if (e1 || e2) {
			xlog("WIFI removing expired station %", *s->ssid != 0 ? s->ssid : s->smac);
			ZEROP(s);
		}
	}
}

// TODO dump to file
static void dump() {
	int sc = 0;
	int zc = 0;

	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac)
			sc++;

	for (int i = 0; i < CLIENTS; i++)
		if (zombies->clients[i].mac)
			zc++;

	xlog("\n\n### %d Stations ### %d Zombies ### %d Lines ###", sc, zc, line_count);

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
		return xerr("Error creating listener_thread");

	line_count = 0;
	xlog("WIFI listening on port %d", PORT);
	return 0;
}

static void stop() {
	if (pthread_cancel(listener_thread))
		xlog("Error canceling listener_thread");

	if (pthread_join(listener_thread, NULL))
		xlog("Error joining listener_thread");

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
