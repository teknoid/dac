// gcc -DWIFI_MAIN -I./include -o wifi mcp.c utils.c wifi.c

// iw phy phy1 interface add mon1 type monitor
// ifconfig mon1 up
// tcpdump -nevi mon1 | tee -a /ram/tcpdump.log | nc tron 6666
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
#include <arpa/inet.h>

#include "utils.h"
#include "wifi.h"
#include "mqtt.h"
#include "mcp.h"

#define PORT					6666

#define BROADCAST				0xffffffffffff
#define IPV6_MCAST				0x333300000000
#define IPV4_MCAST				0x01005e000000
#define STP						0x0180c2000000
#define U2MASK					0xffff00000000
#define U3MASK					0xffffff000000
#define ZSTATION				0xaaffeeaaffee

#define SECONDS_1D 				60 * 60 * 24
#define SECONDS_1HX 			60 * 60 + 300
#define SECONDS_1H 				60 * 60
#define SECONDS_30M				60 * 30
#define SECONDS_10M				60 * 10

#define WIFI_DUMP				"wifi.txt"
#define WIFI_STATE				"wifi.bin"

#define NAME(x)					(*x->name ? x->name : *x->ssid ? x->ssid : x->smac)

static station_t stations[STATIONS];
static station_t *zombies = &stations[STATIONS - 1];

static pthread_mutex_t lock;
static pthread_t thread;
static time_t now_ts;

static unsigned long line_count = 0;
static int dump_line;
static int server_fd;

static station_t* station(uint64_t mac, int create, char *ssid, int channel, int signal) {
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
			if (channel)
				s->channel = channel;
			if (signal)
				s->signal = signal;

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
			uint642oui(mac, s->oui, 64);
			uint642name(mac, s->name, 64);
			if (ssid && strlen(ssid) > 0)
				strcpy(s->ssid, ssid);
			if (channel)
				s->channel = channel;
			if (signal)
				s->signal = signal;

			xlog("WIFI new station %s (%s)", s->smac, NAME(s));
			dump_line = 1;

			return s;
		}

	xerr("WIFI stations table is full!");
	return 0;
}

static client_t* client(station_t *s, uint64_t mac, int create, char *ssid, int channel, int signal, char tag) {
	if (mac == 0 || mac == BROADCAST || mac == STP || mac == s->mac || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST)
		return 0;

	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac == mac) {
			client_t *c = &(s->clients[i]);

			// zombies: create new entry if ssid is different
			if (s == zombies)
				if (ssid && strcmp(ssid, c->ssid))
					continue;

			// client found
			int age = now_ts - c->ts;
			if (age > SECONDS_1HX) {
				xlog("WIFI client %s station %s is back", NAME(c), NAME(s));
				// notify("client is back", c->smac, "au.wav");
			}

			c->count++;
			c->ts = now_ts;
			c->tag = tag;
			if (ssid && strlen(ssid) > 0)
				strcpy(c->ssid, ssid);
			if (channel)
				c->channel = channel;
			if (signal)
				c->signal = signal;

			return c;
		}

	// do not create new entry if not found
	if (!create)
		return 0;

	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac == 0) {
			client_t *c = &(s->clients[i]);

			// create new entry
			ZEROP(c);
			c->mac = mac;
			c->count++;
			c->ts = now_ts;
			c->tag = tag;
			uint642mac(mac, c->smac);
			uint642oui(mac, c->oui, 64);
			uint642name(mac, c->name, 64);
			if (ssid && strlen(ssid) > 0)
				strcpy(c->ssid, ssid);
			if (channel)
				c->channel = channel;
			if (signal)
				c->signal = signal;

			xlog("WIFI new client %s assigned to %s", NAME(c), NAME(s));
			dump_line = 1;
			s->dirty = 1;

			return c;
		}

	xerr("WIFI station %s client table is full!", NAME(s));
	return 0;
}

static client_t* client_unassigned(uint64_t mac, char *ssid, int channel, int signal, char tag) {
	if (mac == 0 || mac == BROADCAST)
		return 0;

	for (int i = 0; i < STATIONS; i++) {
		station_t *s = &stations[i];
		client_t *c = client(s, mac, 0, ssid, channel, signal, tag);
		if (c)
			return c; // found
	}

	// not found - create zombie
	client_t *z = client(zombies, mac, 1, ssid, channel, signal, 'z');
	return z;
}

static int channel(int freq) {
	switch (freq) {
	case 2412:
		return 1;
	case 2417:
		return 2;
	case 2422:
		return 3;
	case 2427:
		return 4;
	case 2432:
		return 5;
	case 2437:
		return 6;
	case 2442:
		return 7;
	case 2447:
		return 8;
	case 2452:
		return 9;
	case 2457:
		return 10;
	case 2462:
		return 11;
	case 2467:
		return 12;
	case 2472:
		return 13;
	case 2484:
		return 14;
	default:
		return freq;
	}
}

static void parse(connection_t *conn) {
	conn->line[strlen(conn->line) - 1] = 0; // remove newline
	// xlog("WIFI read line %s %s", conn->ip, conn->line);

	// make a copy for line dumping after strtok()
	strncpy(conn->line_dump, conn->line, LINEBUF);

	uint64_t bssid = 0, sa = 0, da = 0, ra = 0, ta = 0;
	int signal = 0, freq = 0;
	char ssid[64];
	ZERO(ssid);

	// split line into tokens
	char *t, *oldt;
	char *rest = conn->line;
	while ((t = strtok_r(rest, " ", &rest))) {
		if (starts_with("BSSID", t, strlen(t)))
			bssid = mac2uint64(t + 6);

		if (starts_with("SA", t, strlen(t)))
			sa = mac2uint64(t + 3);

		if (starts_with("DA", t, strlen(t)))
			da = mac2uint64(t + 3);

		if (starts_with("RA", t, strlen(t)))
			ra = mac2uint64(t + 3);

		if (starts_with("TA", t, strlen(t)))
			ta = mac2uint64(t + 3);

		if (ends_with("dBm", t, strlen(t)))
			sscanf(t, "%ddBm", &signal);

		if (!strcmp("MHz", t)) {
			long l = strtol(oldt, NULL, 0);
			if (l > 2000)
				freq = l;
		}

		if (!strcmp("Beacon", t) || !strcmp("Probe", t)) {
			char *x = strchr(rest, '(') + 1;
			char *y = strchr(rest, ')');
			if (x != y) {
				size_t len = y - x;
				strncpy(ssid, x, len);
			}
		}

		oldt = t;
	}

	// packet from station or client
	int schannel = 0, cchannel = 0, ssignal = 0, csignal = 0;
	if (bssid && bssid == sa) {
		schannel = channel(freq);
		ssignal = signal;
	} else {
		cchannel = channel(freq);
		csignal = signal;
	}

	pthread_mutex_lock(&lock);
	dump_line = 0;

	// update or create station
	station_t *bss = station(bssid, 1, ssid, schannel, ssignal);
	if (bss) {
		client(bss, sa, 1, ssid, cchannel, csignal, 's');
		client(bss, da, 1, ssid, cchannel, csignal, 'd');
		client(bss, ra, 1, ssid, cchannel, csignal, 'r');
		client(bss, ta, 1, ssid, cchannel, csignal, 't');
	}

	// assign to SA station
	station_t *sas = station(sa, 0, 0, schannel, ssignal);
	if (sas) {
		client(sas, da, 1, ssid, cchannel, csignal, 'd');
		client(sas, ra, 1, ssid, cchannel, csignal, 'r');
		client(sas, ta, 1, ssid, cchannel, csignal, 't');
	}

	// assign to DA station
	station_t *das = station(da, 0, 0, schannel, ssignal);
	if (das) {
		client(das, sa, 1, ssid, cchannel, csignal, 's');
		client(das, ra, 1, ssid, cchannel, csignal, 'r');
		client(das, ta, 1, ssid, cchannel, csignal, 't');
	}

	// assign to RA station
	station_t *ras = station(ra, 0, 0, schannel, ssignal);
	if (ras) {
		client(ras, sa, 1, ssid, cchannel, csignal, 's');
		client(ras, da, 1, ssid, cchannel, csignal, 'd');
		client(ras, ta, 1, ssid, cchannel, csignal, 't');
	}

	// assign to TA station
	station_t *tas = station(ta, 0, 0, schannel, ssignal);
	if (tas) {
		client(tas, sa, 1, ssid, cchannel, csignal, 's');
		client(tas, da, 1, ssid, cchannel, csignal, 'd');
		client(tas, ra, 1, ssid, cchannel, csignal, 'r');
	}

	// search client in all stations or create new zombie
	int assigned = bss || sas || das || ras || tas;
	if (!assigned) {
		client_unassigned(sa, ssid, cchannel, csignal, 's');
		client_unassigned(da, ssid, cchannel, csignal, 'd');
		client_unassigned(ra, ssid, cchannel, csignal, 'r');
		client_unassigned(ta, ssid, cchannel, csignal, 't');
	}

	line_count++;
	if (dump_line)
		xdebug(conn->line_dump);

	pthread_mutex_unlock(&lock);
}

static void* reader(void *arg) {
	connection_t *conn = (connection_t*) arg;

	struct sockaddr_in *sa_in = (struct sockaddr_in*) &conn->address;
	char *ip = inet_ntoa(sa_in->sin_addr);
	strncpy(conn->ip, ip, 16);
	xlog("WIFI new connection from %s", conn->ip);

	// convert socket into file stream
	conn->stream = fdopen(conn->sock, "r");
	if (conn->stream == NULL) {
		xerr("fdopen failed");
		close(conn->sock);
		free(conn);
		return (void*) 0;
	}

	// read line by line
	while (fgets(conn->line, LINEBUF - 1, conn->stream) != NULL)
		parse(conn);

	xlog("WIFI client %s disconnected", conn->ip);
	fclose(conn->stream);
	close(conn->sock);
	free(conn);

	return (void*) 0;
}

static void* listener(void *arg) {
	while (1) {
		connection_t *conn = malloc(sizeof(connection_t));
		conn->addr_len = sizeof(conn->address);
		conn->sock = accept(server_fd, &conn->address, &conn->addr_len);

		if (conn->sock <= 0) {
			xerr("accept failed");
			free(conn);
			return (void*) 0;
		}

		// start new thread
		if (pthread_create(&conn->thread, 0, &reader, (void*) conn)) {
			xerr("Error creating thread");
			free(conn);
			return (void*) 0;
		}

		// detach it
		if (pthread_detach(conn->thread)) {
			xerr("Error detaching thread");
			free(conn);
			return (void*) 0;
		}
	}
}

static void cleanup() {
	pthread_mutex_lock(&lock);
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
				xlog("WIFI zombie %s is station %s -> removing", NAME(z), NAME(s));
				z->mac = 0;
				zombies->dirty = 1;
				break;
			}

			for (int k = 0; k < CLIENTS; k++) {
				client_t *c = &(s->clients[k]);
				if (!c->mac)
					continue;

				if (z->mac == c->mac) {
					xlog("WIFI zombie %s assigned to %s -> removing", NAME(z), NAME(s));
					z->mac = 0;
					zombies->dirty = 1;

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
	pthread_mutex_unlock(&lock);
}

static void expired() {
	pthread_mutex_lock(&lock);
	for (int i = 0; i < STATIONS; i++) {
		station_t *s = &stations[i];
		if (!s->mac)
			continue;

		// update name
		if (!*s->name)
			uint642name(s->mac, s->name, 64);

		int sc = 0;
		for (int j = 0; j < CLIENTS; j++) {
			client_t *c = &(s->clients[j]);
			if (!c->mac)
				continue;

			// update name
			if (!*c->name)
				uint642name(c->mac, c->name, 64);

			// remove expired client
			int age = now_ts - c->ts;
			int e1 = c->count == 1 && age > SECONDS_10M;
			int e2 = c->count < 10 && age > SECONDS_30M;
			int e3 = c->count < 20 && age > SECONDS_1H;
			int e4 = age > SECONDS_1D;
			if (e1 || e2 || e3 || e4) {
				xlog("WIFI removing expired client %s from %s age=%d count=%d", NAME(c), NAME(s), age, c->count);
				c->mac = 0;
				s->dirty = 1;
			} else
				sc++;
		}

		// remove expired station
		int age = now_ts - s->ts;
		int e1 = sc == 0 && s->count < 10 && age > SECONDS_1H;
		int e2 = sc == 0 && age > SECONDS_1D;
		if (e1 || e2) {
			xlog("WIFI removing expired station %s age=%d count=%d", NAME(s), age, s->count);
			s->mac = 0;
		}
	}
	pthread_mutex_unlock(&lock);
}

static void sort() {
	pthread_mutex_lock(&lock);
	// 1. condense
	// 2. sort
	pthread_mutex_unlock(&lock);
}

static void sort_stations() {
	pthread_mutex_lock(&lock);
	// 1. condense
	// 2. sort
	pthread_mutex_unlock(&lock);
}

static void dump() {
	int sc = 0, zc = 0, age = 0;

	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac)
			sc++;

	for (int i = 0; i < CLIENTS; i++)
		if (zombies->clients[i].mac)
			zc++;

	xlog("WIFI %d Stations, %d Zombies, %lu Lines", sc, zc, line_count);

	FILE *fp = fopen(RUN SLASH WIFI_DUMP, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", RUN SLASH WIFI_DUMP);
		return;
	}
	fprintf(fp, "%d Stations, %d Zombies, %lu Lines\n", sc, zc, line_count);

#define HTEMPLATE "%-20s %-35s %-35s %8s %8s %8s %10s %-35s\n"
#define STEMPLATE "\n%-20s %-35s %-35s %8d %8d %8d %10d %-35s\n"
#define CTEMPLATE "%c %-18s %-35s %-35s %8d %8d %8d %10d %-35s\n"

	fprintf(fp, HTEMPLATE, "MAC", "SSID", "Name", "Channel", "Signal", "Age", "Count", "Hardware");
	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac) {
			station_t *s = &stations[i];
			age = now_ts - s->ts;
			fprintf(fp, STEMPLATE, s->smac, s->ssid, s->name, s->channel, s->signal, age, s->count, s->oui);

			for (int j = 0; j < CLIENTS; j++)
				if (s->clients[j].mac) {
					client_t *c = &(s->clients[j]);
					age = now_ts - c->ts;
					fprintf(fp, CTEMPLATE, c->tag, c->smac, c->ssid, c->name, c->channel, c->signal, age, c->count, c->oui);
				}
		}

	fflush(fp);
	fclose(fp);
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

		if (now_ts % 60 == 0) {
			sort();
			dump();
		}

		if (now_ts % 3600 == 0)
			sort_stations();
	}
}

static int init() {
	load_blob(STATE SLASH WIFI_STATE, stations, sizeof(stations));

	strcpy(zombies->ssid, "Zombies");
	zombies->mac = ZSTATION;
	uint642mac(zombies->mac, zombies->smac);

	pthread_mutex_init(&lock, NULL);

	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(PORT);

	// create server socket
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return xerr("socket failed");

	int opt = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
		return xerr("setsockopt failed");

	if (bind(server_fd, (struct sockaddr*) &address, sizeof(address)) < 0)
		return xerr("bind failed");

	if (listen(server_fd, 3) < 0)
		return xerr("listen failed");

	// start listener thread
	if (pthread_create(&thread, NULL, &listener, NULL))
		return xerr("Error creating thread");

	xlog("WIFI listening on port %d", PORT);
	return 0;
}

static void stop() {
	store_blob(STATE SLASH WIFI_STATE, stations, sizeof(stations));

	if (pthread_cancel(thread))
		xerr("Error canceling thread");

	if (pthread_join(thread, NULL))
		xerr("Error joining thread");

	if (server_fd)
		close(server_fd);

	pthread_mutex_destroy(&lock);
}

static int test() {
	char mac1[] = "d4:ca:6e:43:a0:25", mac2[20], oui[128], name[128];
	uint64_t mac = mac2uint64(mac1);
	uint642mac(mac, mac2);
	uint642oui(mac, oui, 128);
	uint642name(mac, name, 128);
	xlog("mac1=%s uint64=%lx mac2=%s oui=%s name=%s", mac1, mac, mac2, oui, name);

	now_ts = time(NULL);
	struct tm now_tm, *now = &now_tm;
	localtime_r(&now_ts, &now_tm);
	xlog("today=%d", now->tm_wday);

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
