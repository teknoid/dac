// gcc -DWIFI_MAIN -DMQTT_HOST=\"mqtt\" -I./include -L./lib/x86_64 -o wifi mcp.c utils.c wifi.c mqtt-tx.c -lmqttc

// iw phy phy1 interface add mon1 type monitor
// ifconfig mon1 up
// tcpdump -nevi mon1 | nc tron 6666
//
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
#define ZMAC					0xaaffeeaaffee

#define SECONDS_1D 				60 * 60 * 24
#define SECONDS_6H 				60 * 60 * 6
#define SECONDS_1HX 			60 * 60 + 300
#define SECONDS_1H 				60 * 60
#define SECONDS_30M				60 * 30
#define SECONDS_10M				60 * 10

#define WIFI_SORTED				"wifi-sorted.txt"
#define WIFI_FLAT				"wifi-flat.txt"
#define WIFI_RAW				"wifi-raw.txt"
#define WIFI_BIN				"wifi.bin"

// cat /usr/share/ieee-data/oui.csv |sort >/usr/share/ieee-data/oui_sorted.csv
// and then manually remove last line (headline)
#define IEEE					"/usr/share/ieee-data/oui_sorted.csv"
#define ETHERS					"/server/mikrotik/INSTALL/mnt/sda1/etc/dnsmasq.d/ethers"

#define CHANNEL(x)				(x ? (2412 - x) / 5 + 1 : 0)
#define NAME(x)					(*x->name ? x->name : *x->ssid ? x->ssid : x->smac)
#define EMPTY(s)				(s == NULL || strlen(s) == 0)

#define SS						(*ss)
#define CC						(*cc)

static station_t stations[STATIONS];
static station_t *pstations[STATIONS + 1];
static station_t *any = &stations[STATIONS - 2];
static station_t *zombies = &stations[STATIONS - 1];

static description_t ethers[0xff];
static description_t ieee[0xffff];
static int ieee_index[0xff];

static pthread_mutex_t lock;
static pthread_t thread;
static time_t now_ts;

static unsigned long line_count = 0;
static int dump_line;
static int server_fd;

static void notify_station_new(station_t *s) {
	xlog("WIFI new station %s (%s)", s->smac, NAME(s));
	dump_line = 1;

	// not if SSID is empty
	if (EMPTY(s->ssid))
		return;

	mqtt_notify("New Station", NAME(s), "au.wav");
	// mcp_notify("New Station", NAME(s), "au.wav", 0);
}

static void notify_client_new(station_t *s, client_t *c) {
	xlog("WIFI station %s assigned client %s", NAME(s), NAME(c));
	dump_line = 1;

	// only for zombies
	if (s != zombies)
		return;

	// not if SSID is empty
	if (EMPTY(c->ssid))
		return;

	mqtt_notify("New Zombie", NAME(c), "au.wav");
	// mcp_notify("New Zombie", NAME(z), "au.wav", 0);
}

static void notify_client_found(station_t *s, client_t *c) {
	// only after 1+ hour
	int age = now_ts - c->ts;
	if (age < SECONDS_1HX)
		return;

	xlog("WIFI station %s client %s is back, age=%d", NAME(s), NAME(c), age);
	dump_line = 1;

	// only once per client (from any station)
	if (s != any)
		return;

	// not for anonymous clients
	if (EMPTY(c->ssid) && EMPTY(c->name))
		return;

	// not for stations
	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac == c->mac)
			return;

	mqtt_notify("client is back", NAME(c), "au.wav");
	// mcp_notify("client is back", NAME(c), "au.wav", 0);
}

void notify_zombie_assigned(station_t *s, client_t *z) {
	char title[128], text[128];

	// not for anonymous zombies
	if (EMPTY(z->ssid) && EMPTY(z->name))
		return;

	snprintf(title, 128, "Zombie %s", NAME(z));
	snprintf(text, 128, "assigned to %s", NAME(s));
	mqtt_notify(title, text, NULL);
	// mcp_notify(title, text, NULL, 0);
}

static const char* get_ethers_name(uint64_t mac) {
	for (int i = 0; i < 0xff; i++)
		if (ethers[i].mac == mac)
			return ethers[i].description;

	return NULL;
}

static const char* get_ieee_ou(uint64_t mac) {
	// use index to calculate from/to search range in ieee table
	int ii = mac >> 40 & 0xff;
	int from = ieee_index[ii];
	if (ii && !from)
		return NULL;
	int jj = ii + 1;
	while (jj < 0xff && !ieee_index[jj])
		jj++;
	int to = ieee_index[jj] ? ieee_index[jj] : 0xffff;

	// xdebug("%012lx -- from=%d to=%d", m, from, to);
	uint64_t m = mac & U3MASK;
	for (int i = from; i < to; i++)
		if (ieee[i].mac == m)
			return ieee[i].description;

	return NULL;
}

static station_t* station(uint64_t mac, int channel, int signal, char *ssid, int create) {
	if (mac == 0 || mac == BROADCAST)
		return 0;

	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac == mac) {
			station_t *s = &stations[i];

			// station found
			s->count++;
			s->ts = now_ts;
			if (channel)
				s->channel = channel;
			if (signal)
				s->signal = signal;
			if (!EMPTY(ssid))
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
			s->channel = channel;
			s->signal = signal ? signal : -888;

			uint642mac(mac, s->smac);
			const char *ou = get_ieee_ou(s->mac);
			if (ou != NULL)
				strcpy(s->ou, ou);
			const char *name = get_ethers_name(s->mac);
			if (name != NULL)
				strcpy(s->name, name);
			if (!EMPTY(ssid))
				strcpy(s->ssid, ssid);

			notify_station_new(s);
			return s;
		}

	xerr("WIFI stations table is full!");
	return 0;
}

static client_t* client(station_t *s, uint64_t mac, int channel, int signal, char *ssid, char tag, int create) {
	if (mac == 0 || mac == BROADCAST || mac == STP || mac == s->mac || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST)
		return 0;

	for (int i = 0; i < CLIENTS; i++) {
		client_t *c = &(s->clients[i]);

		int found = s->clients[i].mac == mac;

		// zombies with same ssid treated as one
		if (s == zombies && ssid != NULL && !strcmp(ssid, c->ssid)) {
			c->mac = mac;
			tag = c->tag; // keep tag
			found = 1;
		}

		if (found) {
			// client found
			notify_client_found(s, c);
			c->count++;
			c->ts = now_ts;
			c->tag = tag;
			if (channel)
				c->channel = channel;
			if (signal)
				c->signal = signal;
			// take over ssid if not station's ssid
			if (ssid != NULL && strcmp(ssid, s->ssid))
				strcpy(c->ssid, ssid);

			return c;
		}
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
			c->channel = channel;
			c->signal = signal;

			uint642mac(mac, c->smac);
			const char *ou = get_ieee_ou(c->mac);
			if (ou != NULL)
				strcpy(c->ou, ou);
			const char *name = get_ethers_name(c->mac);
			if (name != NULL)
				strcpy(c->name, name);
			// take over ssid if not station's ssid
			if (ssid != NULL && strcmp(ssid, s->ssid))
				strcpy(c->ssid, ssid);

			notify_client_new(s, c);
			return c;
		}

	xerr("WIFI station %s client table is full!", NAME(s));
	return 0;
}

static void parse(connection_t *conn) {
	conn->line[strlen(conn->line) - 1] = 0; // remove newline
	// xlog("WIFI read line %s %s", conn->ip, conn->line);

	// make a copy for line dumping after strtok()
	strncpy(conn->line_dump, conn->line, LINEBUF);

	uint64_t bssid = 0, sa = 0, da = 0, ra = 0, ta = 0;
	int signal = 0, freq = 0;
	char ssid[DESCRIPTION];
	ZERO(ssid);

	// split line into tokens
	char *t, *oldt, *rest = conn->line;
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
			if (!signal)
				sscanf(t, "%ddBm", &signal);

		if (!strcmp("MHz", t))
			if (!freq)
				freq = (int) strtol(oldt, NULL, 0);

		if (!strcmp("Beacon", t) || !strcmp("Probe", t)) {
			char *x = strchr(rest, '(') + 1;
			char *y = strchr(rest, ')');
			if (y != x)
				strncpy(ssid, x, (size_t) (y - x));
		}

		oldt = t;
	}

	// packet from station or client
	int schannel = 0, cchannel = 0, ssignal = 0, csignal = 0;
	if (bssid && bssid == sa) {
		schannel = CHANNEL(freq);
		ssignal = signal;
	} else {
		cchannel = CHANNEL(freq);
		csignal = signal;
	}

	pthread_mutex_lock(&lock);
	dump_line = 0;

	// update or create station
	station_t *bss = station(bssid, schannel, ssignal, ssid, 1);
	if (bss) {

		// BSSID present and station found
		client(bss, sa, cchannel, csignal, ssid, 's', 1);
		client(bss, da, cchannel, csignal, ssid, 'd', 1);
		client(bss, ra, cchannel, csignal, ssid, 'r', 1);
		client(bss, ta, cchannel, csignal, ssid, 't', 1);

		client(any, sa, cchannel, csignal, ssid, 's', 1);
		client(any, da, cchannel, csignal, ssid, 'd', 1);
		client(any, ra, cchannel, csignal, ssid, 'r', 1);
		client(any, ta, cchannel, csignal, ssid, 't', 1);

	} else {

		client_t *sac = 0, *dac = 0, *rac = 0, *tac = 0;

		// assign to SA station
		station_t *sas = station(sa, schannel, ssignal, NULL, 0);
		if (sas) {
			client(sas, da, cchannel, csignal, ssid, 'd', 1);
			client(sas, ra, cchannel, csignal, ssid, 'r', 1);
			client(sas, ta, cchannel, csignal, ssid, 't', 1);
			dac = client(any, da, cchannel, csignal, ssid, 'd', 1);
			rac = client(any, ra, cchannel, csignal, ssid, 'r', 1);
			tac = client(any, ta, cchannel, csignal, ssid, 't', 1);
		}

		// assign to DA station
		station_t *das = station(da, schannel, ssignal, NULL, 0);
		if (das) {
			client(das, sa, cchannel, csignal, ssid, 's', 1);
			client(das, ra, cchannel, csignal, ssid, 'r', 1);
			client(das, ta, cchannel, csignal, ssid, 't', 1);
			sac = client(any, sa, cchannel, csignal, ssid, 's', 1);
			rac = client(any, ra, cchannel, csignal, ssid, 'r', 1);
			tac = client(any, ta, cchannel, csignal, ssid, 't', 1);
		}

		// assign to RA station
		station_t *ras = station(ra, schannel, ssignal, NULL, 0);
		if (ras) {
			client(ras, sa, cchannel, csignal, ssid, 's', 1);
			client(ras, da, cchannel, csignal, ssid, 'd', 1);
			client(ras, ta, cchannel, csignal, ssid, 't', 1);
			sac = client(any, sa, cchannel, csignal, ssid, 's', 1);
			dac = client(any, da, cchannel, csignal, ssid, 'd', 1);
			tac = client(any, ta, cchannel, csignal, ssid, 't', 1);
		}

		// assign to TA station
		station_t *tas = station(ta, schannel, ssignal, NULL, 0);
		if (tas) {
			client(tas, sa, cchannel, csignal, ssid, 's', 1);
			client(tas, da, cchannel, csignal, ssid, 'd', 1);
			client(tas, ra, cchannel, csignal, ssid, 'r', 1);
			sac = client(any, sa, cchannel, csignal, ssid, 's', 1);
			dac = client(any, da, cchannel, csignal, ssid, 'd', 1);
			rac = client(any, ra, cchannel, csignal, ssid, 'r', 1);
		}

		// search client if not already found during assignment or finally create zombie
		if (sa && !sac) {
			sac = client(any, sa, cchannel, csignal, ssid, 's', 0);
			if (!sac)
				sac = client(zombies, sa, cchannel, csignal, ssid, 'z', 1);
		}
		if (da && !dac) {
			dac = client(any, da, cchannel, csignal, ssid, 'd', 0);
			if (!dac)
				dac = client(zombies, da, cchannel, csignal, ssid, 'z', 1);
		}
		if (ra && !rac) {
			rac = client(any, ra, cchannel, csignal, ssid, 'r', 0);
			if (!rac)
				rac = client(zombies, ra, cchannel, csignal, ssid, 'z', 1);
		}
		if (ta && !tac) {
			tac = client(any, ta, cchannel, csignal, ssid, 't', 0);
			if (!tac)
				tac = client(zombies, ta, cchannel, csignal, ssid, 'z', 1);
		}
	}

	line_count++;
	conn->line_count++;
	if (dump_line)
		xdebug(conn->line_dump);

	pthread_mutex_unlock(&lock);
}

static void* reader(void *arg) {
	connection_t *conn = (connection_t*) arg;

	// get client ip address
	struct sockaddr_in *sa_in = (struct sockaddr_in*) &conn->address;
	char *ip = inet_ntoa(sa_in->sin_addr);
	strncpy(conn->ip, ip, 16);
	xlog("WIFI new connection from %s", conn->ip);

	// read line by line
	while (fgets(conn->line, LINEBUF - 1, conn->stream) != NULL)
		parse(conn);

	xlog("WIFI client %s disconnected, received %d lines", conn->ip, conn->line_count);
	fclose(conn->stream);
	close(conn->sock);
	free(conn);

	pthread_exit(NULL);
}

static void* listener(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xerr("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		connection_t *conn = malloc(sizeof(connection_t));
		conn->addr_len = sizeof(conn->address);

		// wait for client connection
		conn->sock = accept(server_fd, &conn->address, &conn->addr_len);
		if (conn->sock <= 0) {
			xerr("accept failed");
			break;
		}

		// convert socket into file stream for reading line by line
		conn->stream = fdopen(conn->sock, "r");
		if (conn->stream == NULL) {
			xerr("fdopen failed");
			break;
		}

		// start new thread
		if (pthread_create(&conn->thread, 0, &reader, (void*) conn)) {
			xerr("Error creating thread");
			break;
		}

		// detach it
		if (pthread_detach(conn->thread)) {
			xerr("Error detaching thread");
			break;
		}
	}

	pthread_exit(NULL);
}

static void assign() {
	pthread_mutex_lock(&lock);

	for (int i = 0; i < CLIENTS; i++) {
		client_t *z = &(zombies->clients[i]);
		if (!z->mac)
			continue;

		// already assigned
		if (z->tag == 'a')
			continue;

		int assigned = 0;
		for (int j = 0; j < STATIONS; j++) {
			station_t *s = &stations[j];
			if (!s->mac || s == zombies || s == any)
				continue;

			if (z->mac == s->mac) {
				xlog("WIFI zombie %s is station %s -> removing", NAME(z), NAME(s));
				z->mac = 0;
				break;
			}

			for (int k = 0; k < CLIENTS; k++) {
				client_t *c = &(s->clients[k]);
				if (!c->mac)
					continue;

				if (z->mac == c->mac) {
					xlog("WIFI zombie %s assigned to %s", NAME(z), NAME(s));

					// take over ssid of probe request
					if (strlen(z->ssid) > 0)
						strcpy(c->ssid, z->ssid);

					notify_zombie_assigned(s, z);
					assigned++;
					break;
				}
			}
		}

		// tag as assigned
		if (assigned)
			z->tag = 'a';
	}

	pthread_mutex_unlock(&lock);
}

static void expired() {
	pthread_mutex_lock(&lock);

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
			int e1 = c->count < 5 && age > SECONDS_10M;
			int e2 = c->count < 10 && age > SECONDS_30M;
			int e3 = c->count < 20 && age > SECONDS_1H;
			int e4 = c->count < 50 && age > SECONDS_6H;
			int e5 = age > SECONDS_1D;
			if (e1 || e2 || e3 || e4 || e5) {
				xlog("WIFI station %s client %s expired, age=%d count=%d", NAME(s), NAME(c), age, c->count);
				c->mac = 0;
			} else
				sc++;
		}

		// remove expired station
		int age = now_ts - s->ts;
		int e1 = sc == 0 && s->count < 10 && age > SECONDS_1H;
		int e2 = sc == 0 && age > SECONDS_1D;
		if (e1 || e2) {
			xlog("WIFI station %s expired, age=%d count=%d", NAME(s), age, s->count);
			s->mac = 0;
		}
	}

	pthread_mutex_unlock(&lock);
}

#define HRAW "%-20s %-35s %-35s %8s %8s %8s %10s %-35s\n"
#define SRAW "\n%-20s %-35s %-35s %8d %8d %8ld %10d %-35s\n"
#define CRAW "%c %-18s %-35s %-35s %8d %8d %8ld %10d %-35s\n"

static void dump_raw() {
	int sc = 0, ac = 0, zc = 0;

	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac)
			sc++;

	for (int i = 0; i < CLIENTS; i++) {
		if (any->clients[i].mac)
			ac++;
		if (zombies->clients[i].mac)
			zc++;
	}

	xlog("WIFI %d Stations, %d Clients, %d Zombies, %lu Lines", sc, ac, zc, line_count);

	FILE *fp = fopen(RUN SLASH WIFI_RAW, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", RUN SLASH WIFI_RAW);
		return;
	}

	fprintf(fp, "%d Stations, %d Clients, %d Zombies, %lu Lines\n", sc, ac, zc, line_count);
	fprintf(fp, HRAW, "MAC", "SSID", "Name", "Channel", "Signal", "Age", "Count", "Hardware");
	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac) {
			station_t *s = &stations[i];
			fprintf(fp, SRAW, s->smac, s->ssid, s->name, s->channel, s->signal, now_ts - s->ts, s->count, s->ou);

			for (int j = 0; j < CLIENTS; j++)
				if (s->clients[j].mac) {
					client_t *c = &(s->clients[j]);
					fprintf(fp, CRAW, c->tag, c->smac, c->ssid, c->name, c->channel, c->signal, now_ts - c->ts, c->count, c->ou);
				}
		}

	fflush(fp);
	fclose(fp);
}

static void dump_sorted() {
	int sc = 0, ac = 0, zc = 0;

	for (station_t **ss = pstations; *ss; ss++)
		sc++;

	for (client_t **aa = any->pclients; *aa; aa++)
		ac++;

	for (client_t **cc = zombies->pclients; *cc; cc++)
		zc++;

	xlog("WIFI %d Stations, %d Clients, %d Zombies, %lu Lines", sc, ac, zc, line_count);

	FILE *fp = fopen(RUN SLASH WIFI_SORTED, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", RUN SLASH WIFI_SORTED);
		return;
	}

	fprintf(fp, "%d Stations, %d Clients, %d Zombies, %lu Lines\n", sc, ac, zc, line_count);
	fprintf(fp, HRAW, "MAC", "SSID", "Name", "Channel", "Signal", "Age", "Count", "Hardware");
	for (station_t **ss = pstations; *ss; ss++) {
		fprintf(fp, SRAW, SS->smac, SS->ssid, SS->name, SS->channel, SS->signal, now_ts - SS->ts, SS->count, SS->ou);
		for (client_t **cc = SS->pclients; *cc; cc++)
			fprintf(fp, CRAW, CC->tag, CC->smac, CC->ssid, CC->name, CC->channel, CC->signal, now_ts - CC->ts, CC->count, CC->ou);
	}

	fflush(fp);
	fclose(fp);
}

#define HFLAT "%-18s %-35s %-25s  %-18s %-35s %-25s %4s %4s %6s %10s %-35s\n"
#define CFLAT "%-18s %-35s %-25s %c %-18s %-35s %-25s %4d %4d %6ld %10d %-35s\n"

static void dump_flat() {
	FILE *fp = fopen(RUN SLASH WIFI_FLAT, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", RUN SLASH WIFI_FLAT);
		return;
	}

	fprintf(fp, HFLAT, "Station MAC", "Station SSID", "Station Name", "Client MAC", "Client SSID", "Client Name", "Chan", "Sig", "Age", "Count", "Hardware");
	for (station_t **ss = pstations; *ss; ss++)
		for (client_t **cc = SS->pclients; *cc; cc++)
			fprintf(fp, CFLAT, SS->smac, SS->ssid, SS->name, CC->tag, CC->smac, CC->ssid, CC->name, CC->channel, CC->signal, now_ts - CC->ts, CC->count, CC->ou);

	fflush(fp);
	fclose(fp);
}

static void sort_clients(station_t *s) {
	// fill client pointer list
	int ii = 0;
	for (int i = 0; i < CLIENTS; i++) {
		client_t *c = &(s->clients[i]);
		if (c->mac)
			s->pclients[ii++] = c;
	}

	// null terminate
	s->pclients[ii] = 0;
	if (!ii)
		return;

	// bubble sort client pointers by count
	for (int i = 0; i < ii - 1; i++)
		for (int j = 0; j < ii - i - 1; j++) {
			client_t *x = s->pclients[j];
			client_t *y = s->pclients[j + 1];
			if (y->count > x->count) {
				s->pclients[j] = y;
				s->pclients[j + 1] = x;
			}
		}
}

static void sort() {
	// fill station pointer list
	int ii = 0;
	for (int i = 0; i < STATIONS; i++) {
		station_t *s = &stations[i];
		if (s->mac) {
			pstations[ii++] = s;
			sort_clients(s);
		}
	}

	// null terminate
	pstations[ii] = 0;
	if (!ii)
		return;

	// bubble sort station pointers by signal
	for (int i = 0; i < ii - 1; i++)
		for (int j = 0; j < ii - i - 1; j++) {
			station_t *x = pstations[j];
			station_t *y = pstations[j + 1];
			if (y->signal > x->signal) {
				pstations[j] = y;
				pstations[j + 1] = x;
			}
		}
}

static int load_ethers() {
	char line[LINEBUF], vv[LINEBUF], name[DESCRIPTION];

	ZERO(ethers);
	FILE *fp = fopen(ETHERS, "rt");
	if (fp == NULL)
		return xerr("UTILS Cannot open file %s for reading", ETHERS);

	int ii = 0;
	while (fgets(line, LINEBUF - 1, fp) != NULL) {

		// not a ether entry
		if (!starts_with("dhcp-host", line, strlen(line)))
			continue;

		// forward to values
		char *v = strchr(line, '=') + 1;

		// remove newline
		v[strlen(v) - 1] = 0;

		// copy line, then split into tokens and find name (next after mac list)
		strncpy(vv, v, LINEBUF - 1);
		char *t, *rest = vv;
		while ((t = strtok_r(rest, ",", &rest))) {
			while (*t == ' ')
				t++; // trim
			if (*(t + 2) != ':' && *(t + 5) != ':' && *(t + 8) != ':')
				break; // not a mac
		}
		while (*(t + strlen(t) - 1) == '\n')
			*(t + strlen(t) - 1) = 0; // trim
		strncpy(name, t, DESCRIPTION - 1);
		// xdebug("line %s :: found name %s", line, name);

		// now go again through line and extract macs
		rest = v;
		while ((t = strtok_r(rest, ",", &rest))) {
			while (*t == ' ')
				t++; // trim
			// xdebug("t=%s rest=%s", t, rest);
			if (*(t + 2) == ':' && *(t + 5) == ':' && *(t + 8) == ':') {
				// pointer to next entry
				description_t *d = &ethers[ii++];
				d->mac = mac2uint64(t);
				strncpy(d->description, name, DESCRIPTION - 1);
			}
		}
	}

	fclose(fp);
	xlog("WIFI loaded %d entries from %s", ii, ETHERS);

	// for (int i = 0; i < ii; i++)
	// xlog("%lx = %s", ethers[i].mac, ethers[i].description);

	return 0;
}

static int load_ieee() {
	char line[LINEBUF], *s, *e;

	ZERO(ieee);
	FILE *fp = fopen(IEEE, "rt");
	if (fp == NULL)
		return xerr("UTILS Cannot open file %s for reading", IEEE);

	int ii = 0;
	while (fgets(line, LINEBUF - 1, fp) != NULL) {

		// pointer to next entry
		description_t *d = &ieee[ii++];

		// Registry
		s = line;
		e = strchr(s + 1, ',');
		*e = 0;

		// Assignment
		s = e + 1;
		e = strchr(s, ',');
		*e = 0;
		d->mac = strtol(s, NULL, 16) << 24;

		// Organization Name
		s = e + 1;
		if (s[0] == '\"') {
			s++;
			e = strchr(s, '\"');
		} else
			e = strchr(s, ',');
		*e = 0;
		strncpy(d->description, s, DESCRIPTION - 1);
	}

	fclose(fp);
	xlog("WIFI loaded %d entries from %s", ii, IEEE);

	// for (int i = 0; i < ii; i++)
	// xlog("%lx = %s", ieee[i].mac, ieee[i].description);

	// create index of highest byte
	ZERO(ieee_index);
	int x = ieee[0].mac >> 40 & 0xff;
	for (int i = 1; i < ii; i++) {
		int y = ieee[i].mac >> 40 & 0xff;
		if (y != x) {
			ieee_index[y] = i;
			x = y;
		}
	}
	ieee_index[0] = 0;

	// for (int i = 0; i < 0xff; i++)
	// xlog("%x = %d", i, ieee_index[i]);

	return 0;
}

static void loop() {
	while (1) {
		sleep(1);
		now_ts = time(NULL);

		any->ts = zombies->ts = now_ts;

		if (now_ts % 10 == 0)
			assign();

		if (now_ts % 30 == 0)
			expired();

		if (now_ts % 60 == 0) {
			sort();
			dump_sorted();
			dump_flat();
			dump_raw();
		}
	}
}

static int init() {
	load_ieee();
	load_ethers();
	load_blob(STATE SLASH WIFI_BIN, stations, sizeof(stations));

	strcpy(zombies->ssid, "Zombies");
	zombies->mac = ZMAC;
	zombies->signal = -999;
	uint642mac(zombies->mac, zombies->smac);

	strcpy(any->ssid, "Any");
	any->mac = ZMAC;
	any->signal = -999;
	uint642mac(any->mac, any->smac);

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

	xlog("WIFI listening on port %d for tcpdump output", PORT);
	return 0;
}

static void stop() {
	store_blob(STATE SLASH WIFI_BIN, stations, sizeof(stations));

	if (pthread_cancel(thread))
		xerr("Error canceling thread");

	if (pthread_join(thread, NULL))
		xerr("Error joining thread");

	if (server_fd)
		close(server_fd);

	pthread_mutex_destroy(&lock);
}

static int test() {
	mcp_init();

	sort();

//	for (station_t **ss = pstations; *ss; ss++)
//		for (client_t **cc = SS->pclients; *cc; cc++)
//			if (CC->mac == 0x860fd0334e65)
//				strcpy(CC->name, "xxx");

	dump_sorted();
	dump_flat();
	dump_raw();

	uint64_t mac;
	mac = mac2uint64("d4:ca:6e:43:a0:25");
	xlog("IEEE %012lx = %s", mac, get_ieee_ou(mac));
	mac = mac2uint64("d4:ca:6f:43:a0:25");
	xlog("IEEE %012lx = %s", mac, get_ieee_ou(mac));

	mac = mac2uint64("c6:7b:dc:17:38:d5");
	xlog("ETHERS %012lx = %s", mac, get_ethers_name(mac));
	mac = mac2uint64("c6:7b:dc:17:38:d6");
	xlog("ETHERS %012lx = %s", mac, get_ethers_name(mac));

	client_t cc, *c = &cc;
	c->mac = ZMAC;
	uint642mac(c->mac, c->smac);
	strcpy(c->name, "Test");
	mqtt_notify("client is back", NAME(c), "au.wav");

	mcp_stop();
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
