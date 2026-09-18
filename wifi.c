// gcc -DWIFI_MAIN -DMQTT_HOST=\"mqtt\" -I./include -L./lib/x86_64 -o wifi wifi.c mcp.c utils.c network.c mqtt-tx.c -lmqttc

// iw phy phy2 interface add mon0 type monitor
// ifconfig mon0 up
// tcpdump -nevi mon0 | nc tron 6666
//
// tcpdump -nevi mon0 | tee -a /ram/tcpdump.log | nc tron 6666
// while true; do for c in `seq 1 13`; do iw dev mon0 set channel $c; sleep 1s; done; done

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <sys/types.h>

#include "network.h"
#include "utils.h"
#include "wifi.h"
#include "mqtt.h"
#include "mcp.h"

#define POPEN					"/usr/bin/tcpdump -nevi mon0"
#define PORT					6666
#define DIRTY					10
#define MIN_COUNT				1000

#define BROADCAST				0xffffffffffff
#define IPV6_MCAST				0x333300000000
#define IPV4_MCAST				0x01005e000000
#define STP						0x0180c2000000
#define U2MASK					0xffff00000000
#define U3MASK					0xffffff000000
#define AFFE					0xaaffeeaaffee
#define DUMMY					0x112233445566

#define SECONDS_1W 				(60 * 60 * 24 * 7)
#define SECONDS_1D 				(60 * 60 * 24)
#define SECONDS_6H 				(60 * 60 * 6)
#define SECONDS_3H 				(60 * 60 * 3)
#define SECONDS_1H 				(60 * 60)
#define SECONDS_5M				(60 * 5)

#define WIFI_COMPACT			"wifi-compact.txt"
#define WIFI_FLAT				"wifi-flat.txt"
#define WIFI_BIN				"wifi.bin"

#define NOTIFY(x, y, z)			mqtt_notify(x, y, z)
//#define NOTIFY(x, y, z)			mcp_notify(x, y, z, 0)

#define CHANNEL(x)				(x ? 1 + (x - 2412) / 5 : 0)
#define NAME(x)					(*x->name ? x->name : *x->ssid ? x->ssid : x->smac)
#define BLACK(x)				(controls(x, 'b', 0) ? 1 : 0)
#define AGE(x)					(x->ts ? now_ts - x->ts : 0)
#define CACHE(x)				(x % 2 ? ocache : ecache)

#define SS						(*ss)
#define CC						(*cc)
#define ZZ						(*zz)

static server_t data, cmnd, local;

static int scount;
static station_t stations[STATIONS];
static station_t *pstations[STATIONS + 1];
static station_t *zombies = &stations[STATIONS - 1];
static station_t *control = &stations[STATIONS - 2];
static station_t *beacons = &stations[STATIONS - 3];
static station_t *ecache = &stations[STATIONS - 4];
static station_t *ocache = &stations[STATIONS - 5];
static station_t *homes = &stations[STATIONS - 6];

static pthread_mutex_t lock;
static time_t now_ts;

static unsigned long line_count = 0;
static int line_dump = 0, popen_x = 0;

static client_t* controls(uint64_t mac, char tag, int op) {
	if (!mac)
		return 0;

	switch (op) {

	case -1:
		// remove when found
		for (int i = 0; i < CLIENTS; i++)
			if (control->clients[i].mac == mac && control->clients[i].tag == tag)
				control->clients[i].mac = 0;
		return 0;

	case 1:
		// return when found
		for (int i = 0; i < CLIENTS; i++)
			if (control->clients[i].mac == mac && control->clients[i].tag == tag)
				return &control->clients[i];
		// insert and return when not found
		for (int i = 0; i < CLIENTS; i++)
			if (control->clients[i].mac == 0) {
				client_t *c = &control->clients[i];
				c->mac = mac;
				c->tag = tag;
				mac2string(c->smac, c->mac);
				mac2ou(c->ou, c->mac, DESCRIPTION);
				return c;
			}
		xerr("WIFI CONTROL table overflow!");
		return 0;

	default:
		// return when found, 0 when not found
		for (int i = 0; i < CLIENTS; i++)
			if (control->clients[i].mac == mac && control->clients[i].tag == tag)
				return &control->clients[i];
		return 0;
	}
}

static client_t* find(station_t *s, uint64_t mac) {
	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac == mac)
			return &s->clients[i];
	return 0;
}

static void notify_new(station_t *s, client_t *c) {
	// not for (calculated) HOMES station
	if (s == homes)
		return;

	xdebug("WIFI station %s assigned new client %s", NAME(s), NAME(c));
	line_dump = 1;

	// not when blacklisted
	if (BLACK(c->mac))
		return;

	// not when in CACHE
	if (find(CACHE(c->mac), c->mac))
		return;

	// not for anonymous stations
	if (EMPTY(s->ssid))
		return;
	if (s == beacons && EMPTY(c->ssid))
		return;

	// not for anonymous clients
	if ((s != beacons && s != zombies) && EMPTY(c->name))
		return;

	NOTIFY(NAME(s), NAME(c), "au.wav");
}

static void notify_back(station_t *s, client_t *c) {
	// not for (calculated) HOMES station
	if (s == homes)
		return;

	// only after 1+ hour
	int age = now_ts - c->ts;
	if (age < SECONDS_1H)
		return;

	xdebug("WIFI station %s client %s is back, age=%d count=%d", NAME(s), NAME(c), age, c->count);
	line_dump = 1;

	// not when blacklisted
	if (BLACK(c->mac))
		return;

	// not when in CACHE
	if (find(CACHE(c->mac), c->mac))
		return;

	// not for anonymous stations
	if (EMPTY(s->ssid))
		return;

	// not for volatile clients
	if (s != zombies && s != beacons && c->count < MIN_COUNT)
		return;

	NOTIFY(NAME(s), NAME(c), "mau2.wav");
}

void notify_assigned(station_t *s, client_t *z) {
	char title[128], text[128];

	// already assigned
	if (z->tag == 'a')
		return;

	// not when blacklisted
	if (BLACK(z->mac))
		return;

	xdebug("WIFI zombie %s assigned to %s", NAME(z), NAME(s));

	snprintf(title, 128, "Zombie %s", NAME(z));
	snprintf(text, 128, "assigned to %s", NAME(s));
	NOTIFY(title, text, NULL);
}

static char* mac2xname(char *name, uint64_t mac, size_t size) {
	client_t *c = controls(mac, 'n', 0);
	if (c)
		return strncpy(name, c->name, size - 1); // name from control

	const char *n = get_ethers_name(mac);
	if (n != NULL)
		return strncpy(name, n, size - 1); // name from ethers

	*name = 0;
	return name;
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
			s->signal = signal ? signal : -999;

			mac2string(s->smac, s->mac);
			mac2xname(s->name, s->mac, DESCRIPTION);
			mac2ou(s->ou, s->mac, DESCRIPTION);
			if (!EMPTY(ssid))
				strcpy(s->ssid, ssid);

			return s;
		}

	xerr("WIFI stations table overflow!");
	return 0;
}

static client_t* client(station_t *s, uint64_t mac, int channel, int signal, char *ssid, char tag, int create) {
	if (mac == 0 || mac == BROADCAST || mac == STP || mac == s->mac || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST)
		return 0;

	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac == mac) {
			client_t *c = &s->clients[i];

			// client found
			notify_back(s, c);
			c->count++;
			c->ts = now_ts;
			c->tag = tag;
			if (channel)
				c->channel = channel;
			if (signal)
				c->signal = signal;
			// take over ssid when different to station and not empty
			if (!EMPTY(ssid))
				if (strcmp(s->ssid, ssid))
					strcpy(c->ssid, ssid);

			return c;
		}

	// do not create new entry if not found
	if (!create)
		return 0;

	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac == 0) {
			client_t *c = &s->clients[i];

			// create new entry
			ZEROP(c);
			c->mac = mac;
			c->count++;
			c->ts = now_ts;
			c->tag = tag;
			c->channel = channel;
			c->signal = signal ? signal : -999;

			mac2string(c->smac, c->mac);
			mac2xname(c->name, c->mac, DESCRIPTION);
			mac2ou(c->ou, c->mac, DESCRIPTION);
			// take over ssid when different to station and not empty
			if (!EMPTY(ssid))
				if (strcmp(s->ssid, ssid))
					strcpy(c->ssid, ssid);

			notify_new(s, c);
			return c;
		}

	xerr("WIFI station %s client table overflow!", NAME(s));
	return 0;
}

static client_t* zombie(uint64_t mac, int channel, int signal, char *ssid) {
	if (mac == 0 || mac == BROADCAST || mac == STP || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST)
		return 0;

	if (EMPTY(ssid))
		return 0; // do not track anonymous zombies

	for (int i = 0; i < CLIENTS; i++)
		if (beacons->clients[i].mac && !strcmp(beacons->clients[i].ssid, ssid))
			return 0; // known station

	for (int i = 0; i < CLIENTS; i++)
		if (zombies->clients[i].mac && !strcmp(zombies->clients[i].ssid, ssid)) {
			client_t *z = &zombies->clients[i];

			// zombie found
			notify_back(zombies, z);
			z->count++;
			z->ts = now_ts;
			if (channel)
				z->channel = channel;
			if (signal)
				z->signal = signal;

			// update mac, smac and ou as long as zombie is unassigned
			if (z->mac != mac && z->tag != 'a') {
				if (!BLACK(z->mac))
					xdebug("WIFI updating zombie %s tag=%c old mac=%012lx new mac=%012lx ", NAME(z), z->tag, z->mac, mac);
				z->mac = mac;
				mac2string(z->smac, z->mac);
				mac2ou(z->ou, z->mac, DESCRIPTION);
			}

			return z;
		}

	for (int i = 0; i < CLIENTS; i++)
		if (zombies->clients[i].mac == 0) {
			client_t *z = &zombies->clients[i];

			// create new entry
			ZEROP(z);
			z->mac = mac;
			z->count++;
			z->ts = now_ts;
			z->tag = 'z';
			z->channel = channel;
			z->signal = signal ? signal : -999;

			mac2string(z->smac, z->mac);
			mac2xname(z->name, z->mac, DESCRIPTION);
			mac2ou(z->ou, z->mac, DESCRIPTION);
			strcpy(z->ssid, ssid);

			notify_new(zombies, z);
			return z;
		}

	xerr("WIFI station %s client table overflow!", NAME(zombies));
	return 0;
}

static client_t* cache(uint64_t bssid, uint64_t mac, int channel, int signal, char *ssid, char tag) {
	if (mac == 0 || mac == bssid || mac == BROADCAST || mac == STP || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST)
		return 0;

	client_t *c = client(CACHE(mac), mac, channel, signal, ssid, tag, 0);
	if (c)
		return c; // already in cache

	// keep cache clean
	uint64_t mac1 = mac + 1, mac2 = mac + 2;
	for (int i = 0; i < CLIENTS; i++) {
		client_t *z = &zombies->clients[i];
		client_t *b = &beacons->clients[i];

		if (z->mac == mac)
			return 0; // is a zombie by mac

		if (b->mac == mac || b->mac == mac1 || b->mac == mac2)
			return 0; // is a station or station client by mac

		if (!EMPTY(ssid)) {
			if (z->mac && !strcmp(z->ssid, ssid))
				return 0; // is a zombie by ssid

			if (b->mac && !strcmp(b->ssid, ssid))
				return 0; // is known probe response or a station by ssid
		}
	}

	return client(CACHE(mac), mac, channel, signal, ssid, tag, 1);
}

static int parse(connection_t *conn) {
//	PROFILING_START

	uint64_t bs = 0, sa = 0, da = 0, ra = 0, ta = 0;
	int signal = 0, freq = 0;
	char ssid[DESCRIPTION];
	ZERO(ssid);

	// split line into tokens
	char *t, *oldt, *rest = conn->line;
	while ((t = strtok_r(rest, " ", &rest))) {
		if (!strncmp("BSSID:", t, 6))
			bs = string2mac(t + 6);

		if (!strncmp("SA:", t, 3))
			sa = string2mac(t + 3);

		if (!strncmp("DA:", t, 3))
			da = string2mac(t + 3);

		if (!strncmp("RA:", t, 3))
			ra = string2mac(t + 3);

		if (!strncmp("TA:", t, 3))
			ta = string2mac(t + 3);

		if (!strcmp("signal", t))
			if (!signal)
				sscanf(oldt, "%ddBm", &signal);

		if (!strcmp("MHz", t))
			if (!freq)
				freq = (int) strtol(oldt, NULL, 0);

		if (!strcmp("Beacon", t) || !strcmp("Probe", t)) {
			char *x = strchr(rest, '(') + 1;
			char *y = strchr(rest, ')');
			if (y != x) {
				size_t size = y - x;
				HICUT(size, DESCRIPTION - 1);
				strncpy(ssid, x, size);
			}
		}

		oldt = t;
	}

	// packet from station or client
	int schannel = 0, cchannel = 0, ssignal = 0, csignal = 0;
	if (bs && bs == sa) {
		schannel = CHANNEL(freq);
		ssignal = signal;
	} else {
		cchannel = CHANNEL(freq);
		csignal = signal;
	}

	pthread_mutex_lock(&lock);
	line_dump = 0;

	// update or insert BEACONS
	client(beacons, bs, schannel, ssignal, ssid, 'b', 1);

	// update or create station
	station_t *bss = station(bs, schannel, ssignal, ssid, 1);
	if (bss) {

		// assign to BSS station
		client(bss, sa, cchannel, csignal, ssid, 's', 1);
		client(bss, da, cchannel, csignal, ssid, 'd', 1);
		client(bss, ra, cchannel, csignal, ssid, 'r', 1);
		client(bss, ta, cchannel, csignal, ssid, 't', 1);

	} else {

		// assign to SA station
		station_t *sas = station(sa, schannel, ssignal, NULL, 0);
		if (sas) {
			client(sas, da, cchannel, csignal, ssid, 'd', 1);
			client(sas, ra, cchannel, csignal, ssid, 'r', 1);
			client(sas, ta, cchannel, csignal, ssid, 't', 1);
		}

		// assign to DA station
		station_t *das = station(da, schannel, ssignal, NULL, 0);
		if (das) {
			client(das, sa, cchannel, csignal, ssid, 's', 1);
			client(das, ra, cchannel, csignal, ssid, 'r', 1);
			client(das, ta, cchannel, csignal, ssid, 't', 1);
		}

		// assign to RA station
		station_t *ras = station(ra, schannel, ssignal, NULL, 0);
		if (ras) {
			client(ras, sa, cchannel, csignal, ssid, 's', 1);
			client(ras, da, cchannel, csignal, ssid, 'd', 1);
			client(ras, ta, cchannel, csignal, ssid, 't', 1);
		}

		// assign to TA station
		station_t *tas = station(ta, schannel, ssignal, NULL, 0);
		if (tas) {
			client(tas, sa, cchannel, csignal, ssid, 's', 1);
			client(tas, da, cchannel, csignal, ssid, 'd', 1);
			client(tas, ra, cchannel, csignal, ssid, 'r', 1);
		}

		// update or insert ZOMBIES
		zombie(sa, cchannel, csignal, ssid);
		zombie(da, cchannel, csignal, ssid);
		zombie(ra, cchannel, csignal, ssid);
		zombie(ta, cchannel, csignal, ssid);
	}

	// update or insert CACHE
	cache(bs, sa, cchannel, csignal, ssid, 's');
	cache(bs, da, cchannel, csignal, ssid, 'd');
	cache(bs, ra, cchannel, csignal, ssid, 'r');
	cache(bs, ta, cchannel, csignal, ssid, 't');

	line_count++;
	if (line_dump)
		xdebug(conn->copy);

	pthread_mutex_unlock(&lock);
//	PROFILING_LOG("parse")

	return 0;
}

static int name(char *smac, char *n) {
	if (EMPTY(smac) || EMPTY(n))
		return xerr("Usage: wifi -n <mac> <name>");

	// update CONTROL
	uint64_t mac = string2mac(smac);
	client_t *c = controls(mac, 'n', 1);
	if (c)
		strncpy(c->name, n, DESCRIPTION - 1);

	// update name in all entries
	for (station_t **ss = pstations; *ss; ss++) {
		for (client_t **cc = SS->pclients; *cc; cc++)
			if (mac == CC->mac) {
				xlog("WIFI station %s updating client %s name '%s'", NAME(SS), NAME(CC), n);
				strncpy(CC->name, n, DESCRIPTION - 1);
			}
		if (mac == SS->mac) {
			xlog("WIFI updating station %s name '%s'", NAME(SS), n);
			strncpy(SS->name, n, DESCRIPTION - 1);
		}
	}

	return 0;
}

static int blacklist(char *smac, int op) {
	if (EMPTY(smac))
		return xerr("Usage: wifi -b <mac> | -w <mac>");

	uint64_t mac = string2mac(smac);
	return controls(mac, 'b', op) ? 0 : -1;
}

static int delete(char *smac) {
	if (EMPTY(smac))
		xerr("Usage: wifi -d <mac>");

	uint64_t mac = string2mac(smac);
	for (station_t **ss = pstations; *ss; ss++) {
		for (client_t **cc = SS->pclients; *cc; cc++)
			if (mac == CC->mac) {
				xlog("WIFI station %s deleting client %s", NAME(SS), NAME(CC));
				CC->mac = 0;
			}
		if (mac == SS->mac) {
			xlog("WIFI deleting station %s", NAME(SS));
			SS->mac = 0;
		}
	}

	return 0;
}

static int command(connection_t *conn) {
	char *rest = conn->line;
	char *cmnd = strtok_r(rest, " ", &rest);
	char *arg1 = strtok_r(rest, " ", &rest);
	char *arg2 = strtok_r(rest, " ", &rest);

	switch (cmnd[0]) {
	case 'b':
		return blacklist(arg1, 1);
	case 'd':
		return delete(arg1);
	case 'n':
		return name(arg1, arg2);
	case 'q':
		return shutdown(conn->sock, SHUT_RDWR);
	case 'w':
		return blacklist(arg1, -1);
	default:
		fprintf(conn->stream, "unknown command %s\n", cmnd);
		fflush(conn->stream);
	}

	return 0;
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
			fprintf(fp, CFLAT, SS->smac, SS->ssid, SS->name, CC->tag, CC->smac, CC->ssid, CC->name, CC->channel, CC->signal, AGE(CC), CC->count, CC->ou);

	fflush(fp);
	fclose(fp);
}

#define HCOMP "%-20s %-35s %-35s %8s %8s %8s %10s %-35s\n"
#define SCOMP "\n%-20s %-35s %-35s %8d %8d %8ld %10d %-35s\n"
#define CCOMP "%c %-18s %-35s %-35s %8d %8d %8ld %10d %-35s\n"
#define TCOMP "%d Stations, %d Zombies, %d Home, %d/%d/%d Cached, %d Controls, %lu Lines"

static void dump_compact() {
	FILE *fp = fopen(RUN SLASH WIFI_COMPACT, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", RUN SLASH WIFI_COMPACT);
		return;
	}

	fprintf(fp, TCOMP, scount, zombies->ccount, homes->ccount, beacons->ccount, ecache->ccount, ocache->ccount, control->ccount, line_count);
	fprintf(fp, "\n\n");
	fprintf(fp, HCOMP, "MAC", "SSID", "Name", "Channel", "Signal", "Age", "Count", "Hardware");
	for (station_t **ss = pstations; *ss; ss++)
		if (SS->mac != AFFE) {
			fprintf(fp, SCOMP, SS->smac, SS->ssid, SS->name, SS->channel, SS->signal, AGE(SS), SS->count, SS->ou);
			for (client_t **cc = SS->pclients; *cc; cc++)
				fprintf(fp, CCOMP, CC->tag, CC->smac, CC->ssid, CC->name, CC->channel, CC->signal, AGE(CC), CC->count, CC->ou);
		}

	fflush(fp);
	fclose(fp);
}

static void dump_station(station_t *s) {
	char filename[DESCRIPTION * 2];
	snprintf(filename, DESCRIPTION * 2, "%s/wifi-%s.txt", RUN, NAME(s));

	FILE *fp = fopen(filename, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", filename);
		return;
	}

	fprintf(fp, HCOMP, "MAC", "SSID", "Name", "Channel", "Signal", "Age", "Count", "Hardware");
	for (client_t **cc = s->pclients; *cc; cc++)
		fprintf(fp, CCOMP, CC->tag, CC->smac, CC->ssid, CC->name, CC->channel, CC->signal, AGE(CC), CC->count, CC->ou);

	fflush(fp);
	fclose(fp);
}

#define TDUMP "\nWIFI %d Stations, %d Zombies, %d Home, %d/%d/%d Cached, %d Controls, %lu Lines"

static void dump() {
//	PROFILING_START

	xlog(TDUMP, scount, zombies->ccount, homes->ccount, beacons->ccount, ecache->ccount, ocache->ccount, control->ccount, line_count);
	dump_compact();
	dump_flat();
	dump_station(zombies);
	dump_station(control);
	dump_station(ecache);
	dump_station(ocache);
	dump_station(beacons);
	dump_station(homes);

//	PROFILING_LOG("dump")
}

// check if zombie is assigned to any station
static void assign() {
//	PROFILING_START

	for (client_t **zz = zombies->pclients; *zz; zz++) {

		int assigned = 0, remove = 0;
		for (station_t **ss = pstations; *ss; ss++) {

			if (SS->mac == AFFE)
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

				notify_assigned(SS, ZZ);
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

// remove expired clients
static void expire() {
//	PROFILING_START

	for (station_t **ss = pstations; *ss; ss++) {

		// CONTROL never expires
		if (SS == control)
			continue;

		// remove expired station
		int age = now_ts - SS->ts;
		int ee = age > SECONDS_1W;
		int e1 = age > SECONDS_3H && SS->ccount == 0 && EMPTY(SS->ssid);
		int e2 = age > SECONDS_6H && SS->ccount == 0 && SS->count < 10;
		int e3 = age > SECONDS_1D && SS->ccount == 0;
		if (ee || e1 || e2 || e3) {
			xdebug("WIFI station %s expired, age=%d count=%d ccount=%d", NAME(SS), age, SS->count, SS->ccount);
			SS->mac = 0;
		}

		// remove expired clients
		for (client_t **cc = SS->pclients; *cc; cc++) {
			int cache = SS == ecache || SS == ocache;
			int keep = SS == zombies || SS == beacons;
			int fake = EMPTY(CC->ou);
			int age = now_ts - CC->ts;
			int ee = age > SECONDS_1W;
			int ec = age > SECONDS_1H && cache;
			int e1 = !keep && age > SECONDS_5M && CC->count < 5 && fake;
			int e2 = !keep && age > SECONDS_1H && CC->count < 10 && fake;
			int e3 = !keep && age > SECONDS_1D && CC->count < 100;
			if (ee || ec || e1 || e2 || e3) {
				// xdebug("WIFI station %s client %s expired, age=%d count=%d", NAME(SS), NAME(CC), age, CC->count);
				CC->mac = 0;
			}
		}

		// keep at least 10 free slots
		int free = CLIENTS - SS->ccount;
		while (free++ < 10) {
			client_t *oldest = &SS->clients[0];
			for (client_t **cc = SS->pclients; *cc; cc++)
				if (CC->mac && CC->ts < oldest->ts)
					oldest = CC;
			xdebug("WIFI station %s force expire %s age=%d", NAME(SS), NAME(oldest), AGE(oldest));
			oldest->mac = 0;
		}
	}

//	PROFILING_LOG("expire")
}

// guess home station for each client
static void home() {
//	PROFILING_START

	ZERO(homes->clients);
	for (station_t **ss = pstations; *ss; ss++) {
		if (SS->mac == AFFE)
			continue;

		for (client_t **cc = SS->pclients; *cc; cc++) {

			// too less counts
			if (CC->count < MIN_COUNT)
				continue;

			// is a station
			if (find(beacons, CC->mac))
				continue;

			// is AVM hardware
			if (!strncmp("AVM", CC->ou, 3))
				continue;

			// track client with maximum count over all stations - assuming this is the home station
			client_t *h = find(homes, CC->mac);
			if (!h)
				h = client(homes, CC->mac, CC->channel, CC->signal, SS->ssid, 'h', 1);
			if (!h)
				continue;
			if (CC->count > h->count) {
				memcpy(h, CC, CLIENT_SIZE);
				strcpy(h->ssid, SS->ssid);
				h->tag = 'h';
			}

			// update time stamp from cache
			client_t *c = find(CACHE(h->mac), h->mac);
			if (c)
				h->ts = c->ts;
		}
	}

//	PROFILING_LOG("home")
}

// copy clients in sorted order and then copy all back
static void reorganize(station_t *s) {
	if (s->dirty < DIRTY)
		return; // not needed

	//	xdebug("WIFI station %s reorganization needed", NAME(s));
	station_t copy;
	ZERO(copy);
	int count = 0;
	for (client_t **cc = s->pclients; *cc; cc++)
		memcpy(&(copy.clients[count++]), CC, CLIENT_SIZE);
	memcpy(&s->clients, &copy.clients, CLIENT_SIZE * CLIENTS);
}

// update client pointer
static void pointers(station_t *s) {
	int count = 0;
	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac)
			s->pclients[count++] = &s->clients[i];
	s->pclients[count] = 0; // null terminate
	s->ccount = count;
}

#include "wifi-sort.h"

static void sort_mac(station_t *s) {
	pointers(s);
	bubble_sort_mac(s);
	reorganize(s);
	pointers(s);
}

static void sort_signal(station_t *s) {
	pointers(s);
	bubble_sort_signal(s);
	reorganize(s);
	pointers(s);
}

static void sort_count(station_t *s) {
	pointers(s);
	bubble_sort_count(s);
	reorganize(s);
	pointers(s);
}

static void sort_ts(station_t *s) {
	pointers(s);
	bubble_sort_ts(s);
	reorganize(s);
	pointers(s);
}

static void sort_ssid(station_t *s) {
	pointers(s);
	bubble_sort_ssid(s);
	reorganize(s);
	pointers(s);
}

static void sort_name(station_t *s) {
	pointers(s);
	bubble_sort_name(s);
	reorganize(s);
	pointers(s);
}

static void sort() {
//	PROFILING_START

	pthread_mutex_lock(&lock);

	// update station pointer
	int count = 0;
	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac)
			pstations[count++] = &stations[i];
	pstations[count] = 0; // null terminate
	scount = count;

	// bubble sort station pointers by age
	for (int i = 0; i < count - 1; i++)
		for (int j = 0; j < count - i - 1; j++) {
			station_t *x = pstations[j];
			station_t *y = pstations[j + 1];
			if (y->ts > x->ts) {
				pstations[j] = y;
				pstations[j + 1] = x;
			}
		}

	// initially sort all clients by count
	for (station_t **ss = pstations; *ss; ss++)
		sort_count(SS);

	// sort ZOMBIES by age
	sort_ts(zombies);

	// sort CONTROL by mac and name
	sort_mac(control);
	sort_name(control);

	// sort BEACONS by signal
	sort_signal(beacons);

	// sort HOMES by ssid
	sort_ssid(homes);

	pthread_mutex_unlock(&lock);
//	PROFILING_LOG("sort")
}

int main_popen(int argc, char **argv) {
	popen_x = 1;
	return mcp_main(argc, argv);
}

static int main_name(int argc, char **argv) {
	if (argc != 4)
		return xerr("Usage: wifi -u <mac> <name>");

	load_blob(TMP SLASH WIFI_BIN, stations, sizeof(stations));
	sort();
	name(argv[2], argv[3]);
	store_blob(TMP SLASH WIFI_BIN, stations, sizeof(stations));
	return 0;
}

static int main_delete(char *smac) {
	load_blob(TMP SLASH WIFI_BIN, stations, sizeof(stations));
	sort();
	delete(smac);
	store_blob(TMP SLASH WIFI_BIN, stations, sizeof(stations));
	return 0;
}
static int main_blacklist(char *smac, int op) {
	load_blob(TMP SLASH WIFI_BIN, stations, sizeof(stations));
	sort();
	blacklist(smac, op);
	store_blob(TMP SLASH WIFI_BIN, stations, sizeof(stations));
	return 0;
}

static int main_test() {
	mcp_init();

	uint64_t mac;
	mac = string2mac("d4:ca:6e:43:a0:25");
	xlog("IEEE %012lx = %s", mac, get_ieee_ou(mac));
	mac = string2mac("d4:ca:6f:43:a0:25");
	xlog("IEEE %012lx = %s", mac, get_ieee_ou(mac));

	mac = string2mac("c6:7b:dc:17:38:d5");
	xlog("ETHERS %012lx = %s", mac, get_ethers_name(mac));
	mac = string2mac("c6:7b:dc:17:38:d6");
	xlog("ETHERS %012lx = %s", mac, get_ethers_name(mac));

	client_t cc, *c = &cc;
	c->mac = AFFE;
	mac2string(c->smac, c->mac);
	strcpy(c->name, "Test");
	NOTIFY("client is back", NAME(c), "au.wav");

	client_t *et = find(ecache, ecache->clients[33].mac);
	if (et)
		xlog("found ec 33 in ECACHE %s ", NAME(et));

	client_t *ot = find(ocache, ocache->clients[66].mac);
	if (ot)
		xlog("found oc 66 in ECACHE %s", NAME(ot));

	for (int i = 0; i < CLIENTS; i++)
		if (beacons->clients[i].mac) {
			client_t *s = &beacons->clients[i];

			client_t *ec = find(ecache, s->mac);
			if (ec)
				xlog("found station %s (%s) in ECACHE", s->smac, NAME(s));

			client_t *oc = find(ocache, s->mac);
			if (oc)
				xlog("found station %s (%s) in OCACHE", s->smac, NAME(s));
		}

	for (station_t **ss = pstations; *ss; ss++)
		for (client_t **cc = SS->pclients; *cc; cc++) {
			client_t *s1 = find(beacons, CC->mac + 1);
			if (s1)
				xlog("station %s client %s (%s) is +1 station of %s (%s)", NAME(SS), CC->smac, NAME(CC), s1->smac, NAME(s1));
			client_t *s2 = find(beacons, CC->mac + 2);
			if (s2)
				xlog("station %s client %s (%s) is +2 station of %s (%s)", NAME(SS), CC->smac, NAME(CC), s2->smac, NAME(s2));
		}

	dump_compact();
	dump_flat();

	mcp_stop();
	return 0;
}

static void loop() {
	while (1) {
		sleep(1);
		now_ts = zombies->ts = control->ts = beacons->ts = ecache->ts = ocache->ts = homes->ts = time(NULL);
		// xdebug("loop %d", SECONDS_1D - (now_ts % SECONDS_1D));

		if (now_ts % 10 == 0)
			sort();

		if (now_ts % 15 == 0)
			assign();

		if (now_ts % 30 == 0)
			expire();

		if (now_ts % 60 == 0)
			dump();

		if (now_ts % SECONDS_5M == 0)
			home();

		if (now_ts % SECONDS_1D == 0)
			store_blob(STATE SLASH WIFI_BIN, stations, sizeof(stations));
	}
}

static int init() {
	pthread_mutex_init(&lock, NULL);
	now_ts = zombies->ts = control->ts = beacons->ts = ecache->ts = ocache->ts = homes->ts = time(NULL);

	load_ieee();
	load_ethers();
	load_blob(TMP SLASH WIFI_BIN, stations, sizeof(stations));
	sort(); // initially update station / client pointers

	strcpy(zombies->ssid, "ZOMBIES");
	zombies->mac = AFFE;
	mac2string(zombies->smac, zombies->mac);

	strcpy(control->ssid, "CONTROL");
	control->mac = AFFE;
	mac2string(control->smac, control->mac);

	strcpy(beacons->ssid, "BEACONS");
	beacons->mac = AFFE;
	mac2string(beacons->smac, beacons->mac);

	strcpy(ecache->ssid, "ECACHE");
	ecache->mac = AFFE;
	mac2string(ecache->smac, ecache->mac);

	strcpy(ocache->ssid, "OCACHE");
	ocache->mac = AFFE;
	mac2string(ocache->smac, ocache->mac);

	strcpy(homes->ssid, "HOMES");
	homes->mac = AFFE;
	mac2string(homes->smac, homes->mac);

	// start local tcpdump thread
	if (popen_x)
		init_popen(&local, "tcpdump", POPEN, &parse);

	// start data and command servers
	init_server(&data, "tcpdump", PORT, &parse);
	init_server(&cmnd, "command", PORT + 1, &command);

	return 0;
}

static void stop() {
	store_blob(TMP SLASH WIFI_BIN, stations, sizeof(stations));

	if (local.thread) {
		pthread_cancel(local.thread);
		pthread_join(local.thread, NULL);
	}

	if (data.thread) {
		pthread_cancel(data.thread);
		pthread_join(data.thread, NULL);
	}

	if (cmnd.thread) {
		pthread_cancel(cmnd.thread);
		pthread_join(cmnd.thread, NULL);
	}

	if (data.sock)
		close(data.sock);

	if (cmnd.sock)
		close(cmnd.sock);

	pthread_mutex_destroy(&lock);
}

int wifi_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	int c;
	while ((c = getopt(argc, argv, "b:d:ln:pt")) != -1) {
		switch (c) {
		case 'b':
			return main_blacklist(optarg, 1);
		case 'd':
			return main_delete(optarg);
		case 'l':
			return mcp_main(argc, argv);
		case 'n':
			return main_name(argc, argv);
		case 'p':
			return main_popen(argc, argv);
		case 't':
			return main_test();
		case 'w':
			return main_blacklist(optarg, -1);
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
