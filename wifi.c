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
#include <netinet/tcp.h>
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
//#define DUMMY					0x112233445566
#define ZMAC					0xaaffeeaaffee

#define SECONDS_1W 				60 * 60 * 24 * 7
#define SECONDS_1D 				60 * 60 * 24
#define SECONDS_1HX 			60 * 60 + 300
#define SECONDS_1H 				60 * 60
#define SECONDS_5M				60 * 5

#define WIFI_COMPACT			"wifi-compact.txt"
#define WIFI_FLAT				"wifi-flat.txt"
#define WIFI_BIN				"wifi.bin"
//#define ZOMBIES_BIN				"zombies.bin"

// cat /usr/share/ieee-data/oui.csv |sort >/usr/share/ieee-data/oui_sorted.csv
// and then manually remove last line (headline)
#define IEEE					"/usr/share/ieee-data/oui_sorted.csv"
#define ETHERS					"/server/mikrotik/INSTALL/mnt/sda1/etc/dnsmasq.d/ethers"

#define CHANNEL(x)				(x ? 1 + (x - 2412) / 5 : 0)
#define NAME(x)					(*x->name ? x->name : *x->ssid ? x->ssid : x->smac)

#define SS						(*ss)
#define CC						(*cc)
#define ZZ						(*zz)

static int scount;
static station_t stations[STATIONS];
static station_t *pstations[STATIONS + 1];
static station_t *zombies = &stations[STATIONS - 1];
static station_t *cache = &stations[STATIONS - 2];

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
	xdebug("WIFI new station %s", NAME(s));
	dump_line = 1;

	mqtt_notify("New Station", NAME(s), "au.wav");
	// mcp_notify("New Station", NAME(s), "au.wav", 0);
}

static void notify_client_new(station_t *s, client_t *c) {
	xdebug("WIFI station %s assigned new client %s", NAME(s), NAME(c));
	dump_line = 1;

	// only for zombies
	if (s != zombies)
		return;

	mqtt_notify("New Zombie", NAME(c), "au.wav");
	// mcp_notify("New Zombie", NAME(z), "au.wav", 0);
}

static void notify_client_found(station_t *s, client_t *c) {
	// only after 1+ hour
	int age = now_ts - c->ts;
	if (age < SECONDS_1HX)
		return;

	xdebug("WIFI station %s client %s is back, age=%d", NAME(s), NAME(c), age);
	dump_line = 1;

	// not when in CACHE station
	for (int i = 0; i < CLIENTS; i++)
		if (cache->clients[i].mac == c->mac)
			return;

	// not for anonymous clients
	if (EMPTY(c->ssid) && EMPTY(c->name))
		return;

	// not for stations
	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac == c->mac)
			return;

	if (s == zombies) {
		mqtt_notify("Zombie is back", NAME(c), "au.wav");
		// mcp_notify("Zombie is back", NAME(c), "au.wav", 0);
	} else {
		mqtt_notify("Client is back", NAME(c), "au.wav");
		// mcp_notify("Client is back", NAME(c), "au.wav", 0);
	}
}

void notify_zombie_assigned(station_t *s, client_t *z) {
	char title[128], text[128];

	// already assigned
	if (z->tag == 'a')
		return;

	xdebug("WIFI zombie %s assigned to %s", NAME(z), NAME(s));

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

			uint642mac(s->mac, s->smac);
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

static client_t* client(station_t *s, uint64_t mac, int channel, int signal, char *ssid, char tag) {
	if (mac == 0 || mac == BROADCAST || mac == STP || mac == s->mac || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST)
		return 0;

	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac != 0) {
			client_t *c = &(s->clients[i]);

			// zombies match on ssid, all others on mac
			int match = s == zombies ? !strcmp(c->ssid, ssid) : c->mac == mac;
			if (!match)
				continue;

			// client found
			notify_client_found(s, c);

			c->count++;
			c->ts = now_ts;
			if (s == zombies) {
				c->mac = mac; // update zombies mac and smac
				uint642mac(c->mac, c->smac);
			} else
				c->tag = tag; // update tag on all others
			if (channel)
				c->channel = channel;
			if (signal)
				c->signal = signal;
			// take over ssid when different to station
			if (strcmp(s->ssid, ssid))
				strcpy(c->ssid, ssid);

			return c;
		}

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

			uint642mac(c->mac, c->smac);
			const char *ou = get_ieee_ou(c->mac);
			if (ou != NULL)
				strcpy(c->ou, ou);
			const char *name = get_ethers_name(c->mac);
			if (name != NULL)
				strcpy(c->name, name);
			// take over ssid when different to station
			if (strcmp(s->ssid, ssid))
				strcpy(c->ssid, ssid);

			notify_client_new(s, c);
			return c;
		}

	xerr("WIFI station %s client table is full!", NAME(s));
	return 0;
}

static void check_ssid(uint64_t mac, int channel, int signal, char *ssid) {
	for (int i = 0; i < STATIONS - 2; i++)
		if (!strcmp(stations[i].ssid, ssid))
			return; // already known

	// create new zombie for unknown ssid
	client(zombies, mac, channel, signal, ssid, 'z');
}

static void parse(connection_t *conn) {
//	PROFILING_START

	conn->line[strlen(conn->line) - 1] = 0; // remove newline
	// xlog("WIFI read line %s %s", conn->ip, conn->line);

	// make a copy for line dumping after strtok()
	memcpy(conn->line_dump, conn->line, LINEBUF);

	uint64_t bssid = 0, sa = 0, da = 0, ra = 0, ta = 0;
	int signal = 0, freq = 0;
	char ssid[DESCRIPTION];
	ZERO(ssid);

	// split line into tokens
	char *t, *oldt, *rest = conn->line;
	while ((t = strtok_r(rest, " ", &rest))) {
		if (!strncmp("BSSID:", t, 6))
			bssid = mac2uint64(t + 6);

		if (!strncmp("SA:", t, 3))
			sa = mac2uint64(t + 3);

		if (!strncmp("DA:", t, 3))
			da = mac2uint64(t + 3);

		if (!strncmp("RA:", t, 3))
			ra = mac2uint64(t + 3);

		if (!strncmp("TA:", t, 3))
			ta = mac2uint64(t + 3);

		if (!strcmp("signal", t))
			if (!signal)
				sscanf(oldt, "%ddBm", &signal);

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

		// assign to BSS station
		client(bss, sa, cchannel, csignal, ssid, 's');
		client(bss, da, cchannel, csignal, ssid, 'd');
		client(bss, ra, cchannel, csignal, ssid, 'r');
		client(bss, ta, cchannel, csignal, ssid, 't');

	} else {

		client_t *sac = 0, *dac = 0, *rac = 0, *tac = 0;

		// assign to SA station
		station_t *sas = station(sa, schannel, ssignal, NULL, 0);
		if (sas) {
			dac = client(sas, da, cchannel, csignal, ssid, 'd');
			rac = client(sas, ra, cchannel, csignal, ssid, 'r');
			tac = client(sas, ta, cchannel, csignal, ssid, 't');
		}

		// assign to DA station
		station_t *das = station(da, schannel, ssignal, NULL, 0);
		if (das) {
			sac = client(das, sa, cchannel, csignal, ssid, 's');
			rac = client(das, ra, cchannel, csignal, ssid, 'r');
			tac = client(das, ta, cchannel, csignal, ssid, 't');
		}

		// assign to RA station
		station_t *ras = station(ra, schannel, ssignal, NULL, 0);
		if (ras) {
			sac = client(ras, sa, cchannel, csignal, ssid, 's');
			dac = client(ras, da, cchannel, csignal, ssid, 'd');
			tac = client(ras, ta, cchannel, csignal, ssid, 't');
		}

		// assign to TA station
		station_t *tas = station(ta, schannel, ssignal, NULL, 0);
		if (tas) {
			sac = client(tas, sa, cchannel, csignal, ssid, 's');
			dac = client(tas, da, cchannel, csignal, ssid, 'd');
			rac = client(tas, ra, cchannel, csignal, ssid, 'r');
		}

		if (sa && !sac && !sas && strlen(ssid))
			check_ssid(sa, cchannel, csignal, ssid);

		if (da && !dac && !das && strlen(ssid))
			check_ssid(da, cchannel, csignal, ssid);

		if (ra && !rac && !ras && strlen(ssid))
			check_ssid(ra, cchannel, csignal, ssid);

		if (ta && !tac && !tas && strlen(ssid))
			check_ssid(ta, cchannel, csignal, ssid);
	}

	// update or insert CACHE station
	client(cache, sa, cchannel, signal, ssid, 's');
	client(cache, da, cchannel, signal, ssid, 'd');
	client(cache, ra, cchannel, signal, ssid, 'r');
	client(cache, ta, cchannel, signal, ssid, 't');

	line_count++;
	conn->line_count++;
	if (dump_line)
		xdebug(conn->line_dump);

	pthread_mutex_unlock(&lock);
//	PROFILING_LOG("parse")
}

static void* reader(void *arg) {
	connection_t *conn = (connection_t*) arg;

	// read line by line
	while (fgets(conn->line, LINEBUF - 1, conn->stream) != NULL)
		parse(conn);

	xlog("WIFI client %s disconnected, received %d lines", conn->ip, conn->line_count);
	fclose(conn->stream);
	close(conn->sock);
	free(conn);

	pthread_exit(NULL);
}

static void* server(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL))
		return xerrv("Error setting pthread_setcancelstate");

	// create server socket
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return xerrv("socket failed");

	// tune buffers
	int opt = 1, bufsize = LINEBUF * 10;
	setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	setsockopt(server_fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
	setsockopt(server_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));

	struct sockaddr_in address;
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(PORT);

	if (bind(server_fd, (struct sockaddr*) &address, sizeof(address)) < 0)
		return xerrv("bind failed");

	if (listen(server_fd, SOMAXCONN) < 0)
		return xerrv("listen failed");

	xlog("WIFI listening on port %d for tcpdump output", PORT);
	while (1) {
		connection_t *conn = malloc(CONNECTION_SIZE);
		conn->addr_len = sizeof(conn->address);

		// wait for client connection
		conn->sock = accept(server_fd, &conn->address, &conn->addr_len);
		if (conn->sock <= 0)
			return xerrv("accept failed");

		// get client ip address
		struct sockaddr_in *sa_in = (struct sockaddr_in*) &conn->address;
		char *ip = inet_ntoa(sa_in->sin_addr);
		strncpy(conn->ip, ip, 16);
		xlog("WIFI new connection from %s", conn->ip);

		// tune buffers
		int opt = 1, bufsize = LINEBUF * 10;
		setsockopt(conn->sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
		setsockopt(conn->sock, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
		setsockopt(conn->sock, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));

		// convert socket into file stream for reading line by line
		conn->stream = fdopen(conn->sock, "r");
		if (conn->stream == NULL)
			return xerrv("fdopen failed");

		// start new thread
		if (pthread_create(&conn->thread, 0, &reader, (void*) conn))
			return xerrv("Error creating thread");

		// detach it
		if (pthread_detach(conn->thread))
			return xerrv("Error detaching thread");
	}

	return (void*) 0;
}

#define HCOMP "%-20s %-35s %-35s %8s %8s %8s %10s %-35s\n"
#define SCOMP "\n%-20s %-35s %-35s %8d %8d %8ld %10d %-35s\n"
#define CCOMP "%c %-18s %-35s %-35s %8d %8d %8ld %10d %-35s\n"

static void dump_compact() {
	FILE *fp = fopen(RUN SLASH WIFI_COMPACT, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", RUN SLASH WIFI_COMPACT);
		return;
	}

	fprintf(fp, "%d Stations, %d Cached, %d Zombies, %lu Lines\n\n", scount, cache->ccount, zombies->ccount, line_count);
	fprintf(fp, HCOMP, "MAC", "SSID", "Name", "Channel", "Signal", "Age", "Count", "Hardware");
	for (station_t **ss = pstations; *ss; ss++) {
		fprintf(fp, SCOMP, SS->smac, SS->ssid, SS->name, SS->channel, SS->signal, now_ts - SS->ts, SS->count, SS->ou);
		for (client_t **cc = SS->pclients; *cc; cc++)
			fprintf(fp, CCOMP, CC->tag, CC->smac, CC->ssid, CC->name, CC->channel, CC->signal, now_ts - CC->ts, CC->count, CC->ou);
	}

	fflush(fp);
	fclose(fp);
}

#define HFLAT "%-18s %-35s %-25s %s %-18s %-35s %-25s %4s %4s %6s %10s %-35s\n"
#define CFLAT "%-18s %-35s %-25s %c %-18s %-35s %-25s %4d %4d %6ld %10d %-35s\n"

static void dump_flat() {
	FILE *fp = fopen(RUN SLASH WIFI_FLAT, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", RUN SLASH WIFI_FLAT);
		return;
	}

	fprintf(fp, HFLAT, "Station MAC", "Station SSID", "Station Name", "T", "Client MAC", "Client SSID", "Client Name", "Chan", "Sig", "Age", "Count", "Hardware");
	for (station_t **ss = pstations; *ss; ss++)
		for (client_t **cc = SS->pclients; *cc; cc++)
			fprintf(fp, CFLAT, SS->smac, SS->ssid, SS->name, CC->tag, CC->smac, CC->ssid, CC->name, CC->channel, CC->signal, now_ts - CC->ts, CC->count, CC->ou);

	fflush(fp);
	fclose(fp);
}

static void assign() {
//	PROFILING_START

	for (client_t **zz = zombies->pclients; *zz; zz++) {

		int assigned = 0, remove = 0;
		for (station_t **ss = pstations; *ss; ss++) {

			if (SS == cache || SS == zombies)
				continue;

			if (ZZ->mac == SS->mac) {
				xdebug("WIFI zombie %s is station %s -> removing", NAME(ZZ), NAME(SS));
				remove++;
				break;
			}

			for (client_t **cc = SS->pclients; *cc; cc++) {
				if (ZZ->mac != CC->mac)
					continue;

				assigned++;
				if (!strcmp(SS->ssid, ZZ->ssid))
					// assigned to correct station
					remove++;
				else
					// take over zombie's ssid (probe request)
					strcpy(CC->ssid, ZZ->ssid);
				notify_zombie_assigned(SS, ZZ);
				break;
			}
		}

		// mark as assigned
		if (ZZ->tag == 'z' && assigned)
			ZZ->tag = 'a';
		if (ZZ->tag == 'u' && assigned)
			ZZ->tag = 'a';

		// mark as unassigned
		if (ZZ->tag == 'a' && !assigned)
			ZZ->tag = 'u';

		// remove
		if (remove)
			ZZ->mac = 0;
	}

//	PROFILING_LOG("assign")
}

static void expired() {
//	PROFILING_START

	for (station_t **ss = pstations; *ss; ss++) {

		// remove expired station
		int age = now_ts - SS->ts;
		int e1 = SS->ccount == 0 && age > SECONDS_1D;
		if (e1) {
			// xdebug("WIFI expired station %s, age=%d count=%d", NAME(s), age, s->count);
			SS->mac = 0;
		}

		// remove expired clients
		for (client_t **cc = SS->pclients; *cc; cc++) {
			int age = now_ts - CC->ts;
			int ec = SS == cache && age > SECONDS_1H;
			int ez = SS == zombies && age > SECONDS_1W;
			int e1 = SS != zombies && CC->count < 5 && age > SECONDS_5M;
			int e2 = SS != zombies && CC->count < 10 && age > SECONDS_1H;
			int e3 = SS != zombies && CC->count < 100 && age > SECONDS_1D;
			int e4 = age > SECONDS_1W;
			if (ec || ez || e1 || e2 || e3 || e4) {
				// xdebug("WIFI expired station %s client %s, age=%d count=%d", NAME(s), NAME(c), age, c->count);
				CC->mac = 0;
			}
		}
	}

//	PROFILING_LOG("expired")
}

static void sort_station(station_t *s) {
	station_t copy;
	int count;

	// update client pointer
	count = 0;
	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac)
			s->pclients[count++] = &s->clients[i];
	s->pclients[count] = 0; // null terminate
	s->ccount = count;

	// empty
	if (!count)
		return;

	// bubble sort client pointers by count
	s->dirty = 0;
	for (int i = 0; i < count - 1; i++)
		for (int j = 0; j < count - i - 1; j++) {
			client_t *x = s->pclients[j];
			client_t *y = s->pclients[j + 1];
			if (y->count > x->count) {
				s->pclients[j] = y;
				s->pclients[j + 1] = x;
				s->dirty++;
			}
		}

	if (s->dirty < 10)
		return;

//	xdebug("WIFI station %s reorganization needed", NAME(s));

	// copy clients in sorted order and then copy all back
	memset(&copy, 0, STATION_SIZE);
	count = 0;
	for (client_t **cc = s->pclients; *cc; cc++)
		memcpy(&(copy.clients[count++]), CC, CLIENT_SIZE);
	memcpy(&s->clients, &copy.clients, CLIENT_SIZE * CLIENTS);

	// update client pointer again
	count = 0;
	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac)
			s->pclients[count++] = &s->clients[i];
	s->pclients[count] = 0; // null terminate
	s->ccount = count;
}

static void sort_stations() {
//	PROFILING_START
	pthread_mutex_lock(&lock);

	// update station pointer
	int count = 0;
	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac)
			pstations[count++] = &stations[i];
	pstations[count] = 0; // null terminate
	scount = count;

	// bubble sort station pointers by signal
	for (int i = 0; i < count - 1; i++)
		for (int j = 0; j < count - i - 1; j++) {
			station_t *x = pstations[j];
			station_t *y = pstations[j + 1];
			if (y->signal > x->signal) {
				pstations[j] = y;
				pstations[j + 1] = x;
			}
		}

	// sort station clients
	for (station_t **ss = pstations; *ss; ss++)
		sort_station(SS);

	pthread_mutex_unlock(&lock);
//	PROFILING_LOG("sort stations")
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
		now_ts = cache->ts = zombies->ts = time(NULL);

		if (now_ts % 10 == 0)
			sort_stations();

		if (now_ts % 15 == 0)
			assign();

		if (now_ts % 30 == 0)
			expired();

		if (now_ts % 60 == 0) {
			dump_compact();
			dump_flat();
			xdebug("\nWIFI %d Stations, %d Cached, %d Zombies, %lu Lines", scount, cache->ccount, zombies->ccount, line_count);
		}
	}
}

static int init() {
	pthread_mutex_init(&lock, NULL);
	now_ts = cache->ts = zombies->ts = time(NULL);

	load_ieee();
	load_ethers();
	load_blob(STATE SLASH WIFI_BIN, stations, sizeof(stations));
//	load_blob(STATE SLASH ZOMBIES_BIN, zombies->clients, CLIENT_SIZE * CLIENTS);

	strcpy(cache->ssid, "CACHE");
	cache->mac = ZMAC;
	cache->signal = -998;
	uint642mac(cache->mac, cache->smac);

	strcpy(zombies->ssid, "ZOMBIES");
	zombies->mac = ZMAC;
	zombies->signal = -999;
	uint642mac(zombies->mac, zombies->smac);

	// initially update station / client pointers
	sort_stations();

	// start server thread
	if (pthread_create(&thread, NULL, &server, NULL))
		return xerr("Error creating thread");

	return 0;
}

static void stop() {
//	store_blob(STATE SLASH ZOMBIES_BIN, zombies->clients, CLIENT_SIZE * CLIENTS);
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

	dump_compact();
	dump_flat();

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
