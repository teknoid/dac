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

#define CHANNEL(x)				(x ? 1 + (x - 2412) / 5 : 0)
#define NAME(x)					(*x->name ? x->name : *x->ssid ? x->ssid : x->smac)
#define AGE(x)					(x->ts ? now_ts - x->ts : 0)

#define ZOMBIES(x)				find(zombies, x->mac)
#define BEACONS(x)				find(beacons, x->mac)
#define CACHE(x)				find(cache, x->mac)
#define BLACK(x)				find(black, x->mac)
#define NAMES(x)				find(names, x->mac)
#define HOMES(x)				find(homes, x->mac)

#define NOTIFY(x, y, z)			mqtt_notify(x, y, z)
//#define NOTIFY(x, y, z)			mcp_notify(x, y, z, 0)

#define SS						(*ss)
#define CC						(*cc)
#define ZZ						(*zz)

static wifi_t wifi;
static meta_station_t *zombies = &wifi.zombie;
static meta_station_t *beacons = &wifi.beacon;
static meta_station_t *cache = &wifi.cache;
static meta_station_t *black = &wifi.black;
static meta_station_t *names = &wifi.name;
static meta_station_t *homes = &wifi.home;

static server_t data, cmnd, local;
static pthread_mutex_t lock;
static time_t now_ts;

static unsigned long line_count = 0;
static int line_dump = 0, popen_x = 0;

static char* mac2xname(char *name, uint64_t mac, size_t size) {
	for (int i = 0; i < CLIENTS4; i++)
		if (names->clients[i].mac == mac)
			return strncpy(name, names->clients[i].name, size - 1); // name from NAMES

	const char *n = get_ethers_name(mac);
	if (n != NULL)
		return strncpy(name, n, size - 1); // name from ETHERS

	*name = 0;
	return name;
}

static client_t* find(meta_station_t *s, uint64_t mac) {
	for (int i = 0; i < CLIENTS4; i++)
		if (s->clients[i].mac == mac)
			return &s->clients[i];
	return 0;
}

static void notify_new(char *bssid, client_t *c) {
	xdebug("WIFI station %s assigned new client %s", bssid, NAME(c));
	line_dump = 1;

	// not when in CACHE
	if (CACHE(c))
		return;

	// not when blacklisted
	if (BLACK(c))
		return;

	// not for anonymous stations
	if (EMPTY(bssid))
		return;

	// not for anonymous clients
	int skip = strcmp(META_ZOMBIE, bssid) || strcmp(META_BEACON, bssid);
	if (EMPTY(c->name) && skip)
		return;

	NOTIFY(bssid, NAME(c), "au.wav");
}

static void notify_back(char *bssid, client_t *c) {
	// only after 1+ hour
	int age = AGE(c);
	if (age < SECONDS_1H)
		return;

	xdebug("WIFI station %s client %s is back, age=%d count=%d", bssid, NAME(c), age, c->count);
	line_dump = 1;

	// not when in CACHE
	if (CACHE(c))
		return;

	// not when blacklisted
	if (BLACK(c))
		return;

	// not for anonymous stations
	if (EMPTY(bssid))
		return;

	// not for volatile clients
	int skip = strcmp(META_ZOMBIE, bssid) || strcmp(META_BEACON, bssid);
	if (c->count < MIN_COUNT && skip)
		return;

	NOTIFY(bssid, NAME(c), "mau2.wav");
}

void notify_assigned(station_t *s, client_t *z) {
	// already assigned
	if (z->tag == 'a')
		return;

	// not when blacklisted
	if (BLACK(z))
		return;

	xdebug("WIFI zombie %s assigned to %s", NAME(z), NAME(s));

	char title[DESCRIPTION2], text[DESCRIPTION2];
	snprintf(title, DESCRIPTION2, "Zombie %s", NAME(z));
	snprintf(text, DESCRIPTION2, "assigned to %s", NAME(s));
	NOTIFY(title, text, NULL);
}

static station_t* station(uint64_t mac, int channel, int signal, char *ssid, int create) {
	if (mac == 0 || mac == BROADCAST)
		return 0;

	for (int i = 0; i < STATIONS; i++)
		if (wifi.station[i].mac == mac) {
			station_t *s = &wifi.station[i];

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
		if (wifi.station[i].mac == 0) {
			station_t *s = &wifi.station[i];

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

static client_t* update(client_t *c, uint64_t mac, int channel, int signal, char *ssid, char *bssid, char tag) {
	notify_back(bssid, c);

	// update mac, smac and ou as long as zombie is unassigned
	if (mac && mac != c->mac && c->tag != 'a') {
		c->mac = mac;
		mac2string(c->smac, c->mac);
		mac2ou(c->ou, c->mac, DESCRIPTION);
		if (!BLACK(c))
			xdebug("WIFI updating station %s client %s tag=%c old mac=%012lx new mac=%012lx ", bssid, NAME(c), c->tag, c->mac, mac);
	}

	c->count++;
	c->ts = now_ts;
	if (tag)
		c->tag = tag;
	if (channel)
		c->channel = channel;
	if (signal)
		c->signal = signal;

	// take over ssid when different to station and not empty
	if (!EMPTY(ssid))
		if (strcmp(bssid, ssid))
			strcpy(c->ssid, ssid);

	return c;
}

static client_t* insert(client_t *c, uint64_t mac, int channel, int signal, char *ssid, char *bssid, char tag) {
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
		if (strcmp(bssid, ssid))
			strcpy(c->ssid, ssid);

	notify_new(bssid, c);
	return c;
}

static client_t* client(station_t *s, uint64_t mac, int channel, int signal, char *ssid, char tag) {
	if (mac == 0 || mac == BROADCAST || mac == STP || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST || mac == s->mac)
		return 0;

	// client found
	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac == mac)
			return update(&s->clients[i], 0, channel, signal, ssid, s->ssid, tag);

	// create new entry
	for (int i = 0; i < CLIENTS; i++)
		if (s->clients[i].mac == 0)
			return insert(&s->clients[i], mac, channel, signal, ssid, s->ssid, tag);

	xerr("WIFI station %s client table overflow!", NAME(s));
	return 0;
}

static client_t* meta(meta_station_t *s, uint64_t mac, int channel, int signal, char *ssid, char tag) {
	if (mac == 0 || mac == BROADCAST || mac == STP || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST)
		return 0;

	// client found
	for (int i = 0; i < CLIENTS4; i++)
		if (s->clients[i].mac == mac)
			return update(&s->clients[i], 0, channel, signal, ssid, s->ssid, tag);

	// create new entry
	for (int i = 0; i < CLIENTS4; i++)
		if (s->clients[i].mac == 0)
			return insert(&s->clients[i], mac, channel, signal, ssid, s->ssid, tag);

	xerr("WIFI station %s client table overflow!", NAME(s));
	return 0;
}

static client_t* zombie(uint64_t mac, int channel, int signal, char *ssid) {
	if (mac == 0 || mac == BROADCAST || mac == STP || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST)
		return 0;

	// do not track anonymous zombies
	if (EMPTY(ssid))
		return 0;

	// known station
	for (int i = 0; i < CLIENTS4; i++)
		if (beacons->clients[i].mac && !strcmp(beacons->clients[i].ssid, ssid))
			return 0;

	// zombie found
	for (int i = 0; i < CLIENTS4; i++)
		if (zombies->clients[i].mac && !strcmp(zombies->clients[i].ssid, ssid))
			return update(&zombies->clients[i], mac, channel, signal, ssid, zombies->ssid, 0);

	// create new entry
	for (int i = 0; i < CLIENTS4; i++)
		if (zombies->clients[i].mac == 0)
			return insert(&zombies->clients[i], mac, channel, signal, ssid, zombies->ssid, 'z');

	xerr("WIFI ZOMBIES client table overflow!");
	return 0;
}

static int parse(connection_t *conn) {
//	PROFILING_START

	pthread_mutex_lock(&lock);
	line_dump = 0;

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

	// update or insert BEACONS
	meta(beacons, bs, schannel, ssignal, ssid, 'b');

	// update or create station
	station_t *bss = station(bs, schannel, ssignal, ssid, 1);
	if (bss) {

		// assign to BSS station
		client(bss, sa, cchannel, csignal, ssid, 's');
		client(bss, da, cchannel, csignal, ssid, 'd');
		client(bss, ra, cchannel, csignal, ssid, 'r');
		client(bss, ta, cchannel, csignal, ssid, 't');

	} else {

		// assign to SA station
		station_t *sas = station(sa, schannel, ssignal, NULL, 0);
		if (sas) {
			client(sas, da, cchannel, csignal, ssid, 'd');
			client(sas, ra, cchannel, csignal, ssid, 'r');
			client(sas, ta, cchannel, csignal, ssid, 't');
		}

		// assign to DA station
		station_t *das = station(da, schannel, ssignal, NULL, 0);
		if (das) {
			client(das, sa, cchannel, csignal, ssid, 's');
			client(das, ra, cchannel, csignal, ssid, 'r');
			client(das, ta, cchannel, csignal, ssid, 't');
		}

		// assign to RA station
		station_t *ras = station(ra, schannel, ssignal, NULL, 0);
		if (ras) {
			client(ras, sa, cchannel, csignal, ssid, 's');
			client(ras, da, cchannel, csignal, ssid, 'd');
			client(ras, ta, cchannel, csignal, ssid, 't');
		}

		// assign to TA station
		station_t *tas = station(ta, schannel, ssignal, NULL, 0);
		if (tas) {
			client(tas, sa, cchannel, csignal, ssid, 's');
			client(tas, da, cchannel, csignal, ssid, 'd');
			client(tas, ra, cchannel, csignal, ssid, 'r');
		}

		// update or insert ZOMBIES
		zombie(sa, cchannel, csignal, ssid);
		zombie(da, cchannel, csignal, ssid);
		zombie(ra, cchannel, csignal, ssid);
		zombie(ta, cchannel, csignal, ssid);
	}

	// update or insert CACHE
	meta(cache, sa, cchannel, csignal, ssid, 's');
	meta(cache, da, cchannel, csignal, ssid, 'd');
	meta(cache, ra, cchannel, csignal, ssid, 'r');
	meta(cache, ta, cchannel, csignal, ssid, 't');

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

	// insert or update NAMES
	uint64_t mac = string2mac(smac);
	client_t *c = meta(names, mac, 0, 0, NULL, 'n');
	if (c) {
		c->ts = c->signal = c->count = 0;
		strncpy(c->name, n, DESCRIPTION - 1);
	}

	// update name in all station entries
	for (station_t **ss = wifi.pstation; *ss; ss++) {
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

	// update name in all meta entries
	for (meta_station_t **ss = wifi.pmeta; *ss; ss++) {
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

	// insert
	if (op == 1) {
		client_t *c = meta(black, mac, 0, 0, NULL, 'b');
		if (c)
			c->ts = c->signal = c->count = 0;
		return 0;
	}

	// delete
	if (op == -1) {
		client_t *c = find(black, mac);
		if (c)
			c->mac = 0;
		return 0;
	}

	return 0;
}

static int delete(char *smac) {
	if (EMPTY(smac))
		xerr("Usage: wifi -d <mac>");

	uint64_t mac = string2mac(smac);

	// delete all station entries
	for (station_t **ss = wifi.pstation; *ss; ss++) {
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

	// delete all meta entries
	for (meta_station_t **ss = wifi.pmeta; *ss; ss++) {
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
	for (station_t **ss = wifi.pstation; *ss; ss++)
		for (client_t **cc = SS->pclients; *cc; cc++)
			fprintf(fp, CFLAT, SS->smac, SS->ssid, SS->name, CC->tag, CC->smac, CC->ssid, CC->name, CC->channel, CC->signal, AGE(CC), CC->count, CC->ou);

	fflush(fp);
	fclose(fp);
}

#define HCOMP "%-20s %-35s %-35s %8s %8s %8s %10s %-35s\n"
#define SCOMP "\n%-20s %-35s %-35s %8d %8d %8ld %10d %-35s\n"
#define CCOMP "%c %-18s %-35s %-35s %8d %8d %8ld %10d %-35s\n"
#define TCOMP "%d Stations, %d Zombies, %d Home, %d Beacons,  %d Cached, %lu Lines"

static void dump_compact() {
	FILE *fp = fopen(RUN SLASH WIFI_COMPACT, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", RUN SLASH WIFI_COMPACT);
		return;
	}

	fprintf(fp, TCOMP, wifi.station_count, zombies->ccount, homes->ccount, beacons->ccount, cache->ccount, line_count);
	fprintf(fp, "\n\n");
	fprintf(fp, HCOMP, "MAC", "SSID", "Name", "Channel", "Signal", "Age", "Count", "Hardware");
	for (station_t **ss = wifi.pstation; *ss; ss++) {
		fprintf(fp, SCOMP, SS->smac, SS->ssid, SS->name, SS->channel, SS->signal, AGE(SS), SS->count, SS->ou);
		for (client_t **cc = SS->pclients; *cc; cc++)
			fprintf(fp, CCOMP, CC->tag, CC->smac, CC->ssid, CC->name, CC->channel, CC->signal, AGE(CC), CC->count, CC->ou);
	}

	fflush(fp);
	fclose(fp);
}

static void dump_meta(meta_station_t *s) {
	char filename[DESCRIPTION2];
	snprintf(filename, DESCRIPTION2, "%s/wifi-%s.txt", RUN, NAME(s));

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

#define TDUMP "\nWIFI %d Stations, %d Zombies, %d Home, %d Beacons,  %d Cached, %lu Lines"

static void dump() {
//	PROFILING_START

	xlog(TDUMP, wifi.station_count, zombies->ccount, homes->ccount, beacons->ccount, cache->ccount, line_count);
	dump_compact();
	dump_flat();
	dump_meta(beacons);
	dump_meta(zombies);
	dump_meta(cache);
	dump_meta(black);
	dump_meta(names);
	dump_meta(homes);

//	PROFILING_LOG("dump")
}

// check if zombie is assigned to any station
static void assign() {
//	PROFILING_START

	for (client_t **zz = zombies->pclients; *zz; zz++) {

		int assigned = 0, remove = 0;
		for (station_t **ss = wifi.pstation; *ss; ss++) {
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

static int station_expired(station_t *s) {
	int age = AGE(s);

	if (age > SECONDS_3H && s->ccount == 0 && EMPTY(s->ssid))
		return 1;

	if (age > SECONDS_6H && s->ccount == 0 && s->count < 10)
		return 1;

	if (age > SECONDS_1D && s->ccount == 0)
		return 1;

	if (age > SECONDS_1W)
		return 1;

	return 0;
}

static int client_expired(client_t *c) {
	int age = AGE(c);
	int fake = EMPTY(c->ou);

	if (age > SECONDS_5M && c->count < 5 && fake)
		return 1;

	if (age > SECONDS_1H && c->count < 10 && fake)
		return 1;

	if (age > SECONDS_1D && c->count < 100)
		return 1;

	if (age > SECONDS_1W)
		return 1;

	return 0;
}

static int beacon_expired(client_t *c) {
	return AGE(c) > SECONDS_1W;
}

static int zombie_expired(client_t *c) {
	return AGE(c) > SECONDS_1W;
}

static int cache_expired(client_t *c) {
	return AGE(c) > SECONDS_1H;
}

// remove expired clients
static void expire() {
//	PROFILING_START

	for (station_t **ss = wifi.pstation; *ss; ss++) {

		// remove expired station
		if (station_expired(SS)) {
			xdebug("WIFI station %s expired, age=%d count=%d ccount=%d", NAME(SS), AGE(SS), SS->count, SS->ccount);
			SS->mac = 0;
		}

		// remove expired clients
		for (client_t **cc = SS->pclients; *cc; cc++)
			if (client_expired(CC))
				CC->mac = 0;

	}

	// remove expired BEACONS
	for (client_t **cc = beacons->pclients; *cc; cc++)
		if (beacon_expired(CC))
			CC->mac = 0;

	// remove expired ZOMBIES
	for (client_t **cc = zombies->pclients; *cc; cc++)
		if (zombie_expired(CC))
			CC->mac = 0;

	// remove expired CACHE
	for (client_t **cc = cache->pclients; *cc; cc++)
		if (cache_expired(CC))
			CC->mac = 0;

//	PROFILING_LOG("expire")
}

// guess home station for each client
static void home() {
//	PROFILING_START

	ZERO(homes->clients);
	for (station_t **ss = wifi.pstation; *ss; ss++) {
		for (client_t **cc = SS->pclients; *cc; cc++) {

			// too less counts
			if (CC->count < MIN_COUNT)
				continue;

			// is a station
			if (BEACONS(CC))
				continue;

			// is AVM hardware
			if (!strncmp("AVM", CC->ou, 3))
				continue;

			// track client with maximum count over all stations - assuming this is the home station
			client_t *h = HOMES(CC);
			if (!h)
				h = meta(homes, CC->mac, CC->channel, CC->signal, SS->ssid, 'h');
			if (!h)
				continue;
			if (CC->count > h->count) {
				memcpy(h, CC, CLIENT_SIZE);
				strcpy(h->ssid, SS->ssid);
				h->tag = 'h';
			}

			// update time stamp from cache
			client_t *c = CACHE(h);
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

static void reorganize_meta(meta_station_t *s) {
	if (s->dirty < DIRTY)
		return; // not needed

	//	xdebug("WIFI station %s reorganization needed", NAME(s));
	meta_station_t copy;
	ZERO(copy);
	int count = 0;
	for (client_t **cc = s->pclients; *cc; cc++)
		memcpy(&(copy.clients[count++]), CC, CLIENT_SIZE);
	memcpy(&s->clients, &copy.clients, CLIENT_SIZE * CLIENTS4);
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

static void pointers_meta(meta_station_t *s) {
	int count = 0;
	for (int i = 0; i < CLIENTS4; i++)
		if (s->clients[i].mac)
			s->pclients[count++] = &s->clients[i];
	s->pclients[count] = 0; // null terminate
	s->ccount = count;
}

#include "wifi-sort.h"

static void sort_count(station_t *s) {
	pointers(s);
	bubble_sort_count(s);
	reorganize(s);
	pointers(s);
}

static void sort_count_meta(meta_station_t *s) {
	pointers_meta(s);
	bubble_sort_count_meta(s);
	reorganize_meta(s);
	pointers_meta(s);
}

static void sort_signal(meta_station_t *s) {
	pointers_meta(s);
	bubble_sort_signal(s);
	reorganize_meta(s);
	pointers_meta(s);
}

static void sort_ts(meta_station_t *s) {
	pointers_meta(s);
	bubble_sort_ts(s);
	reorganize_meta(s);
	pointers_meta(s);
}

static void sort_ssid(meta_station_t *s) {
	pointers_meta(s);
	bubble_sort_ssid(s);
	reorganize_meta(s);
	pointers_meta(s);
}

static void sort_name(meta_station_t *s) {
	pointers_meta(s);
	bubble_sort_name(s);
	reorganize_meta(s);
	pointers_meta(s);
}

static void sort() {
//	PROFILING_START

	pthread_mutex_lock(&lock);

	// update station pointer
	int count = 0;
	for (int i = 0; i < STATIONS; i++)
		if (wifi.station[i].mac)
			wifi.pstation[count++] = &wifi.station[i];
	wifi.pstation[count] = 0; // null terminate
	wifi.station_count = count;

	// bubble sort station pointers by age
	for (int i = 0; i < count - 1; i++)
		for (int j = 0; j < count - i - 1; j++) {
			station_t *x = wifi.pstation[j];
			station_t *y = wifi.pstation[j + 1];
			if (y->ts > x->ts) {
				wifi.pstation[j] = y;
				wifi.pstation[j + 1] = x;
			}
		}

	// sort all station clients by count
	for (station_t **ss = wifi.pstation; *ss; ss++)
		sort_count(SS);

	// sort BEACONS by signal
	sort_signal(beacons);

	// sort ZOMBIES by age
	sort_ts(zombies);

	// sort CACHE by count
	sort_count_meta(cache);

	// sort BLACK and NAME by name
	sort_name(black);
	sort_name(names);

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

	load_blob(TMP SLASH WIFI_BIN, &wifi, sizeof(wifi));
	sort();
	name(argv[2], argv[3]);
	store_blob(TMP SLASH WIFI_BIN, &wifi, sizeof(wifi));
	return 0;
}

static int main_delete(char *smac) {
	load_blob(TMP SLASH WIFI_BIN, &wifi, sizeof(wifi));
	sort();
	delete(smac);
	store_blob(TMP SLASH WIFI_BIN, &wifi, sizeof(wifi));
	return 0;
}
static int main_blacklist(char *smac, int op) {
	load_blob(TMP SLASH WIFI_BIN, &wifi, sizeof(wifi));
	sort();
	blacklist(smac, op);
	store_blob(TMP SLASH WIFI_BIN, &wifi, sizeof(wifi));
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
	c->mac = DUMMY;
	mac2string(c->smac, c->mac);
	strcpy(c->name, "Test");
	NOTIFY("client is back", NAME(c), "au.wav");

	client_t *t = find(cache, cache->clients[66].mac);
	if (t)
		xlog("found 66 in CACHE %s", NAME(t));

	xlog("### Test stations in CACHE");
	for (int i = 0; i < CLIENTS4; i++)
		if (beacons->clients[i].mac) {
			client_t *s = &beacons->clients[i];

			client_t *c = find(cache, s->mac);
			if (c)
				xlog("found station %s (%s) in OCACHE", s->smac, NAME(s));
		}

	xlog("### Test stations mac+1 mac+2");
	for (station_t **ss = wifi.pstation; *ss; ss++)
		for (client_t **cc = SS->pclients; *cc; cc++) {
			client_t *s1 = find(beacons, c->mac + 1);
			if (s1)
				xlog("station %s client %s (%s) is +1 station of %s (%s)", NAME(SS), c->smac, NAME(c), s1->smac, NAME(s1));
			client_t *s2 = find(beacons, c->mac + 2);
			if (s2)
				xlog("station %s client %s (%s) is +2 station of %s (%s)", NAME(SS), c->smac, NAME(c), s2->smac, NAME(s2));
		}

	dump_compact();
	dump_flat();

	mcp_stop();
	return 0;
}

static void loop() {
	while (1) {
		sleep(1);
		now_ts = time(NULL);
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
			store_blob(STATE SLASH WIFI_BIN, &wifi, sizeof(wifi));
	}
}

static int init() {
	pthread_mutex_init(&lock, NULL);
	now_ts = time(NULL);

	load_ieee();
	load_ethers();
	load_blob(TMP SLASH WIFI_BIN, &wifi, sizeof(wifi));
	sort(); // initially update all pointers

	strcpy(beacons->ssid, META_BEACON);
	strcpy(zombies->ssid, META_ZOMBIE);
	strcpy(cache->ssid, META_CACHE);
	strcpy(black->ssid, META_BLACK);
	strcpy(names->ssid, META_NAME);
	strcpy(homes->ssid, META_HOME);

	wifi.pmeta[0] = beacons;
	wifi.pmeta[1] = zombies;
	wifi.pmeta[2] = cache;
	wifi.pmeta[3] = black;
	wifi.pmeta[4] = names;
	wifi.pmeta[5] = homes;
	wifi.pmeta[6] = 0; // null terminate

	// start local tcpdump thread
	if (popen_x)
		init_popen(&local, "tcpdump", POPEN, &parse);

	// start data and command servers
	init_server(&data, "tcpdump", PORT, &parse);
	init_server(&cmnd, "command", PORT + 1, &command);

	return 0;
}

static void stop() {
	store_blob(TMP SLASH WIFI_BIN, &wifi, sizeof(wifi));

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
