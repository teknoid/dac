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
#define PPH_MIN					10

#define BROADCAST				0xffffffffffff
#define IPV6_MCAST				0x333300000000
#define IPV4_MCAST				0x01005e000000
#define STP						0x0180c2000000
#define U2MASK					0xffff00000000
#define U3MASK					0xffffff000000
#define XFXMASK					0x00ffffffff00
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
#define PPH(x)					(x->ts_last - x->ts_first > 3600 ? x->count / ((int)(x->ts_last - x->ts_first) / 3600) : 0)
#define PPM(x)					(x->ts_last - x->ts_first > 60 ? x->count / ((int)(x->ts_last - x->ts_first) / 60) : 0)
#define AGE(x)					(x->ts_last ? (int)(now_ts - x->ts_last) : 0)

#define ZOMBIES(x)				find(zombies, x->mac)
#define BEACONS(x)				find(beacons, x->mac)
#define CACHE(x)				find(cache, x->mac)
#define BLACK(x)				find(black, x->mac)
#define NAMES(x)				find(names, x->mac)
#define HOMES(x)				find(homes, x->mac)

#define NOTIFY(x, y, z)			mqtt_notify(x, y, z)
//#define NOTIFY(x, y, z)			mcp_notify(x, y, z, 0)

#define SS						(*ss)
#define MM						(*mm)
#define ZZ						(*zz)

static wifi_t wifi;
static big_station_t *zombies = &wifi.zombie;
static big_station_t *beacons = &wifi.beacon;
static big_station_t *cache = &wifi.cache;
static big_station_t *black = &wifi.black;
static big_station_t *names = &wifi.name;
static big_station_t *homes = &wifi.home;

static server_t data, cmnd, local;
static pthread_mutex_t lock;
static time_t now_ts;

static unsigned long line_count = 0;
static int line_dump = 0, popen_x = 0;

static char* mac2xname(char *name, uint64_t mac, size_t size) {
	for (int i = 0; i < CLIENTS4; i++)
		if (names->macs[i].mac == mac)
			return strncpy(name, names->macs[i].name, size); // name from NAMES

	const char *n = get_ethers_name(mac);
	if (n != NULL)
		return strncpy(name, n, size); // name from ETHERS

	*name = 0;
	return name;
}

static mac_t* find(big_station_t *s, uint64_t mac) {
	for (int i = 0; i < CLIENTS4; i++)
		if (s->macs[i].mac == mac)
			return &s->macs[i];
	return 0;
}

static void notify_new(mac_t *s, mac_t *m) {
	// not for (calculated) HOME
	if (s == (mac_t*) homes)
		return;

	xdebug("WIFI station %s assigned new client %s", NAME(s), NAME(m));
	line_dump = 1;

	// ZOMBIE always
	if (s == (mac_t*) zombies) {
		NOTIFY("New Zombie", NAME(m), "au.wav");
		return;
	}

	// BEACON if ssid is present
	if (s == (mac_t*) beacons) {
		if (!EMPTY(m->ssid))
			NOTIFY("New Station", NAME(m), "au.wav");
		return;
	}

	// not when in CACHE
	if (CACHE(m))
		return;

	// not when blacklisted
	if (BLACK(m))
		return;

	// not for anonymous station
	if (EMPTY(s->ssid))
		return;

	// only for named clients
	if (EMPTY(m->name))
		return;

	NOTIFY(NAME(s), NAME(m), "au.wav");
}

static void notify_back(mac_t *s, mac_t *m) {
	// only after 1+ hour
	int age = AGE(m);
	if (age < SECONDS_1H)
		return;

	int pph = PPH(m);
	xdebug("WIFI station %s client %s is back, age=%d rate=%d count=%d", NAME(s), NAME(m), age, pph, m->count);
	line_dump = 1;

	// ZOMBIE always
	if (s == (mac_t*) zombies) {
		NOTIFY("Zombie is back", NAME(m), "mau2.wav");
		return;
	}

	// BEACON if ssid is present
	if (s == (mac_t*) beacons) {
		if (!EMPTY(m->ssid))
			NOTIFY("Station is back", NAME(m), "mau2.wav");
		return;
	}

	// not when in CACHE
	if (CACHE(m))
		return;

	// not when blacklisted
	if (BLACK(m))
		return;

	// not for anonymous stations
	if (EMPTY(s->ssid))
		return;

	// too less packets per hour
	if (pph < PPH_MIN)
		return;

	NOTIFY(NAME(s), NAME(m), "mau2.wav");
}

void notify_assigned(small_station_t *s, mac_t *z) {
	// already assigned
	if (z->tag == 'a')
		return;

	// not when blacklisted
	if (BLACK(z))
		return;

	xdebug("WIFI zombie %s assigned to %s", NAME(z), NAME(s));

	char title[DESCRIPTION2], text[DESCRIPTION2];
	snprintf(title, DESCRIPTION, "Zombie %s", NAME(z));
	snprintf(text, DESCRIPTION, "assigned to %s", NAME(s));
	NOTIFY(title, text, NULL);
}

static small_station_t* station(uint64_t mac, int channel, int signal, char *ssid, int create) {
	if (mac == 0 || mac == BROADCAST)
		return 0;

	for (int i = 0; i < STATIONS; i++)
		if (wifi.station[i].mac == mac) {
			small_station_t *s = &wifi.station[i];

			// station found
			s->count++;
			s->ts_last = now_ts;
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
			small_station_t *s = &wifi.station[i];

			// create new entry
			ZEROP(s);
			s->mac = mac;
			s->count++;
			s->channel = channel;
			s->signal = signal ? signal : -999;
			s->ts_first = s->ts_last = now_ts;

			mac2string(s->smac, s->mac);
			mac2xname(s->name, s->mac, SSID_LEN);
			mac2ou(s->ou, s->mac, DESCRIPTION);
			if (!EMPTY(ssid))
				strcpy(s->ssid, ssid);

			return s;
		}

	xerr("WIFI stations table overflow!");
	return 0;
}

static mac_t* update(mac_t *s, mac_t *m, uint64_t mac, int channel, int signal, char *ssid, char tag) {
	notify_back(s, m);

	// update mac, smac and ou as long as zombie is unassigned
	if (mac && mac != m->mac && m->tag != 'a') {
		m->mac = mac;
		mac2string(m->smac, m->mac);
		mac2ou(m->ou, m->mac, DESCRIPTION);
		if (!BLACK(m))
			xdebug("WIFI updating station %s client %s tag=%c old mac=%012lx new mac=%012lx ", NAME(s), NAME(m), m->tag, m->mac, mac);
	}

	m->count++;
	m->ts_last = now_ts;
	if (tag)
		m->tag = tag;
	if (channel)
		m->channel = channel;
	if (signal)
		m->signal = signal;

	// take over ssid when different to station and not empty
	if (!EMPTY(ssid))
		if (strcmp(s->ssid, ssid))
			strcpy(m->ssid, ssid);

	return m;
}

static mac_t* insert(mac_t *s, mac_t *m, uint64_t mac, int channel, int signal, char *ssid, char tag) {
	ZEROP(m);

	m->mac = mac;
	m->count++;
	m->tag = tag;
	m->channel = channel;
	m->signal = signal ? signal : -999;
	m->ts_first = m->ts_last = now_ts;

	mac2string(m->smac, m->mac);
	mac2xname(m->name, m->mac, SSID_LEN);
	mac2ou(m->ou, m->mac, DESCRIPTION);

	// take over ssid when different to station and not empty
	if (!EMPTY(ssid))
		if (strcmp(s->ssid, ssid))
			strcpy(m->ssid, ssid);

	notify_new(s, m);
	return m;
}

static mac_t* mac_small(small_station_t *s, uint64_t mac, int channel, int signal, char *ssid, char tag) {
	if (mac == 0 || mac == s->mac || mac == BROADCAST || mac == STP || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST)
		return 0;

	// client found
	for (int i = 0; i < CLIENTS; i++)
		if (s->macs[i].mac == mac)
			return update((mac_t*) s, &s->macs[i], 0, channel, signal, ssid, tag);

	// create new entry
	for (int i = 0; i < CLIENTS; i++)
		if (s->macs[i].mac == 0)
			return insert((mac_t*) s, &s->macs[i], mac, channel, signal, ssid, tag);

	xerr("WIFI station %s client table overflow!", NAME(s));
	return 0;
}

static mac_t* mac_big(big_station_t *s, uint64_t mac, int channel, int signal, char *ssid, char tag) {
	if (mac == 0 || mac == s->mac || mac == BROADCAST || mac == STP || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST)
		return 0;

	// client found
	for (int i = 0; i < CLIENTS4; i++)
		if (s->macs[i].mac == mac)
			return update((mac_t*) s, &s->macs[i], 0, channel, signal, ssid, tag);

	// create new entry
	for (int i = 0; i < CLIENTS4; i++)
		if (s->macs[i].mac == 0)
			return insert((mac_t*) s, &s->macs[i], mac, channel, signal, ssid, tag);

	xerr("WIFI station %s client table overflow!", NAME(s));
	return 0;
}

static mac_t* zombie(uint64_t mac, int channel, int signal, char *ssid) {
	if (mac == 0 || mac == BROADCAST || mac == STP || (mac & U2MASK) == IPV6_MCAST || (mac & U3MASK) == IPV4_MCAST)
		return 0;

	// do not track anonymous zombies
	if (EMPTY(ssid))
		return 0;

	// known station
	for (int i = 0; i < CLIENTS4; i++)
		if (beacons->macs[i].mac && !strcmp(beacons->macs[i].ssid, ssid))
			return 0;

	// zombie found
	for (int i = 0; i < CLIENTS4; i++)
		if (zombies->macs[i].mac && !strcmp(zombies->macs[i].ssid, ssid))
			return update((mac_t*) zombies, &zombies->macs[i], mac, channel, signal, ssid, 0);

	// create new entry
	for (int i = 0; i < CLIENTS4; i++)
		if (zombies->macs[i].mac == 0)
			return insert((mac_t*) zombies, &zombies->macs[i], mac, channel, signal, ssid, 'z');

	xerr("WIFI ZOMBIES client table overflow!");
	return 0;
}

static int parse(connection_t *conn) {
//	PROFILING_START

	pthread_mutex_lock(&lock);
	line_dump = 0;

	uint64_t bssid = 0, sa = 0, da = 0, ra = 0, ta = 0;
	int signal = 0, freq = 0;
	char ssid[SSID_LEN * 8 + 1]; // more space for utf8 meta sequences
	ZERO(ssid);

	// split line into tokens
	char *t, *oldt, *rest = conn->line;
	while ((t = strtok_r(rest, " ", &rest))) {
		if (!strncmp("BSSID:", t, 6))
			bssid = string2mac(t + 6);

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
				HICUT(size, SSID_LEN * 8);
				strncpy(ssid, x, size);
				decode_meta_utf8(ssid);
				if (strlen(ssid) > SSID_LEN)
					ssid[SSID_LEN] = 0; // cut to 32 characters - 802.11 spec
			}
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

	// update or insert BEACONS
	mac_big(beacons, bssid, schannel, ssignal, ssid, 'b');

	// update or insert station
	small_station_t *bss = station(bssid, schannel, ssignal, ssid, 1);
	if (bss) {

		// assign to BSS station
		mac_small(bss, sa, cchannel, csignal, ssid, 's');
		mac_small(bss, da, cchannel, csignal, ssid, 'd');
		mac_small(bss, ra, cchannel, csignal, ssid, 'r');
		mac_small(bss, ta, cchannel, csignal, ssid, 't');

	} else {

		// assign to SA station
		small_station_t *sas = station(sa, schannel, ssignal, NULL, 0);
		if (sas) {
			mac_small(sas, da, cchannel, csignal, ssid, 'd');
			mac_small(sas, ra, cchannel, csignal, ssid, 'r');
			mac_small(sas, ta, cchannel, csignal, ssid, 't');
		}

		// assign to DA station
		small_station_t *das = station(da, schannel, ssignal, NULL, 0);
		if (das) {
			mac_small(das, sa, cchannel, csignal, ssid, 's');
			mac_small(das, ra, cchannel, csignal, ssid, 'r');
			mac_small(das, ta, cchannel, csignal, ssid, 't');
		}

		// assign to RA station
		small_station_t *ras = station(ra, schannel, ssignal, NULL, 0);
		if (ras) {
			mac_small(ras, sa, cchannel, csignal, ssid, 's');
			mac_small(ras, da, cchannel, csignal, ssid, 'd');
			mac_small(ras, ta, cchannel, csignal, ssid, 't');
		}

		// assign to TA station
		small_station_t *tas = station(ta, schannel, ssignal, NULL, 0);
		if (tas) {
			mac_small(tas, sa, cchannel, csignal, ssid, 's');
			mac_small(tas, da, cchannel, csignal, ssid, 'd');
			mac_small(tas, ra, cchannel, csignal, ssid, 'r');
		}

		// update or insert ZOMBIES
		zombie(sa, cchannel, csignal, ssid);
		zombie(da, cchannel, csignal, ssid);
		zombie(ra, cchannel, csignal, ssid);
		zombie(ta, cchannel, csignal, ssid);
	}

	// update or insert CACHE
	mac_big(cache, sa, cchannel, csignal, ssid, 's');
	mac_big(cache, da, cchannel, csignal, ssid, 'd');
	mac_big(cache, ra, cchannel, csignal, ssid, 'r');
	mac_big(cache, ta, cchannel, csignal, ssid, 't');

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
	mac_t *m = mac_big(names, mac, 0, 0, NULL, 'n');
	if (m) {
		m->ts_first = m->ts_last = m->signal = m->count = 0;
		strncpy(m->name, n, SSID_LEN);
	}

	// update name in all station entries
	for (small_station_t **ss = wifi.pstation; *ss; ss++) {
		for (mac_t **mm = SS->pmacs; *mm; mm++)
			if (mac == MM->mac) {
				xlog("WIFI station %s updating client %s name '%s'", NAME(SS), NAME(MM), n);
				strncpy(MM->name, n, SSID_LEN);
			}
		if (mac == SS->mac) {
			xlog("WIFI updating station %s name '%s'", NAME(SS), n);
			strncpy(SS->name, n, SSID_LEN);
		}
	}

	// update name in all meta entries
	for (big_station_t **ss = wifi.pmeta; *ss; ss++) {
		for (mac_t **mm = SS->pmacs; *mm; mm++)
			if (mac == MM->mac) {
				xlog("WIFI station %s updating client %s name '%s'", NAME(SS), NAME(MM), n);
				strncpy(MM->name, n, SSID_LEN);
			}
		if (mac == SS->mac) {
			xlog("WIFI updating station %s name '%s'", NAME(SS), n);
			strncpy(SS->name, n, SSID_LEN);
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
		mac_t *m = mac_big(black, mac, 0, 0, NULL, 'b');
		if (m)
			m->ts_first = m->ts_last = m->signal = m->count = 0;
		return 0;
	}

	// delete
	if (op == -1) {
		mac_t *m = find(black, mac);
		if (m)
			m->mac = 0;
		return 0;
	}

	return 0;
}

static int delete(char *smac) {
	if (EMPTY(smac))
		xerr("Usage: wifi -d <mac>");

	uint64_t mac = string2mac(smac);

	// delete all station entries
	for (small_station_t **ss = wifi.pstation; *ss; ss++) {
		for (mac_t **mm = SS->pmacs; *mm; mm++)
			if (mac == MM->mac) {
				xlog("WIFI station %s deleting client %s", NAME(SS), NAME(MM));
				MM->mac = 0;
			}
		if (mac == SS->mac) {
			xlog("WIFI deleting station %s", NAME(SS));
			SS->mac = 0;
		}
	}

	// delete all meta entries
	for (big_station_t **ss = wifi.pmeta; *ss; ss++) {
		for (mac_t **mm = SS->pmacs; *mm; mm++)
			if (mac == MM->mac) {
				xlog("WIFI station %s deleting client %s", NAME(SS), NAME(MM));
				MM->mac = 0;
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
	// xdebug("WIFI command cmnd=%s arg1=%s arg2=%s", cmnd, arg1, arg2);

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

static void evaluate() {
	xlog("\n### find multiple BECONS at same device on similar mac addresses ###");
	for (mac_t **x = beacons->pmacs; *x; x++) {
		uint64_t xmac = (*x)->mac & XFXMASK;
		for (mac_t **y = beacons->pmacs; *y; y++) {
			if (y == x)
				continue; // self
			uint64_t ymac = (*y)->mac & XFXMASK;
			if (ymac == xmac)
				xlog("%s => %s   %s => %s", (*y)->smac, (*x)->smac, NAME((*y)), NAME((*x)));
		}
	}

	xlog("\n### find BEACON as exact client in another station ###");
	for (mac_t **b = beacons->pmacs; *b; b++)
		for (small_station_t **ss = wifi.pstation; *ss; ss++)
			for (mac_t **mm = SS->pmacs; *mm; mm++)
				if ((*b)->mac == MM->mac)
					xlog("%s (%s) is exact client of station %s (%s)", (*b)->smac, NAME((*b)), SS->smac, NAME(SS));

	xlog("\n### find station-station interconnection on similar mac addresses ###");
	for (mac_t **b = beacons->pmacs; *b; b++) {
		uint64_t bmac = (*b)->mac & XFXMASK;
		for (small_station_t **ss = wifi.pstation; *ss; ss++)
			for (mac_t **mm = SS->pmacs; *mm; mm++) {
				uint64_t mmac = MM->mac & XFXMASK;
				if (bmac == mmac)
					xlog("%s (%s) is client %s (%s) of station %s (%s)", (*b)->smac, NAME((*b)), MM->smac, NAME(MM), SS->smac, NAME(SS));
			}
	}
}

#define HFLAT "%-18s %-32s %-25s %s %-18s %-32s %-25s %4s %4s %6s %6s %10s %-40s\n"
#define CFLAT "%-18s %-32s %-25s %c %-18s %-32s %-25s %4d %4d %6d %6d %10d %-40s\n"

static void dump_flat() {
	FILE *fp = fopen(RUN SLASH WIFI_FLAT, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", RUN SLASH WIFI_FLAT);
		return;
	}

	fprintf(fp, HFLAT, "Station MAC", "Station SSID", "Station Name", "T", "Client MAC", "Client SSID", "Client Name", "Chan", "Sig", "Age", "Rate", "Count", "Hardware");
	for (small_station_t **ss = wifi.pstation; *ss; ss++)
		for (mac_t **mm = SS->pmacs; *mm; mm++)
			fprintf(fp, CFLAT, SS->smac, SS->ssid, SS->name, MM->tag, MM->smac, MM->ssid, MM->name, MM->channel, MM->signal, AGE(MM), PPH(MM), MM->count, MM->ou);

	fflush(fp);
	fclose(fp);
}

#define HCOMP "%-20s %-32s %-32s %8s %8s %8s %8s %10s %-64s\n"
#define SCOMP "\n%-20s %-32s %-32s %8d %8d %8d %8d %10d %-64s\n"
#define CCOMP "%c %-18s %-32s %-32s %8d %8d %8d %8d %10d %-64s\n"
#define TCOMP "%d Stations, %d Beacons, %d Zombies, %d Cached, %d Home, %lu Lines"

static void dump_compact() {
	FILE *fp = fopen(RUN SLASH WIFI_COMPACT, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", RUN SLASH WIFI_COMPACT);
		return;
	}

	fprintf(fp, TCOMP, wifi.station_count, beacons->mcount, zombies->mcount, cache->mcount, homes->mcount, line_count);
	fprintf(fp, "\n\n");
	fprintf(fp, HCOMP, "MAC", "SSID", "Name", "Channel", "Signal", "Age", "Rate", "Count", "Hardware");
	for (small_station_t **ss = wifi.pstation; *ss; ss++) {
		fprintf(fp, SCOMP, SS->smac, SS->ssid, SS->name, SS->channel, SS->signal, AGE(SS), PPH(SS), SS->count, SS->ou);
		for (mac_t **mm = SS->pmacs; *mm; mm++)
			fprintf(fp, CCOMP, MM->tag, MM->smac, MM->ssid, MM->name, MM->channel, MM->signal, AGE(MM), PPH(MM), MM->count, MM->ou);
	}

	fflush(fp);
	fclose(fp);
}

static void dump_meta(big_station_t *s) {
	char filename[DESCRIPTION2];
	snprintf(filename, DESCRIPTION2, "%s/wifi-%s.txt", RUN, NAME(s));

	FILE *fp = fopen(filename, "wt");
	if (fp == NULL) {
		xerr("WIFI Cannot open file %s for writing", filename);
		return;
	}

	fprintf(fp, HCOMP, "MAC", "SSID", "Name", "Channel", "Signal", "Age", "Rate", "Count", "Hardware");
	for (mac_t **mm = s->pmacs; *mm; mm++)
		fprintf(fp, CCOMP, MM->tag, MM->smac, MM->ssid, MM->name, MM->channel, MM->signal, AGE(MM), PPH(MM), MM->count, MM->ou);

	fflush(fp);
	fclose(fp);
}

#define TDUMP "\nWIFI %d Stations, %d Beacons, %d Zombies, %d Cached, %d Home, %lu Lines"

static void dump() {
//	PROFILING_START

	xlog(TDUMP, wifi.station_count, beacons->mcount, zombies->mcount, cache->mcount, homes->mcount, line_count);
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

	for (mac_t **zz = zombies->pmacs; *zz; zz++) {

		int assigned = 0, remove = 0;
		for (small_station_t **ss = wifi.pstation; *ss; ss++) {
			if (ZZ->mac == SS->mac) {
				xdebug("WIFI zombie %s is station %s -> removing", NAME(ZZ), NAME(SS));
				remove++;
				break;
			}

			for (mac_t **mm = SS->pmacs; *mm; mm++) {
				if (ZZ->mac != MM->mac)
					continue;

				assigned++;
				if (!strcmp(SS->ssid, ZZ->ssid))
					// assigned to correct station
					remove++;
				else
					// take over zombie's ssid (probe request)
					strcpy(MM->ssid, ZZ->ssid);

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

static int station_expired(small_station_t *s) {
	int age = AGE(s);

	if (age > SECONDS_3H && s->mcount == 0 && EMPTY(s->ssid))
		return 1;

	if (age > SECONDS_6H && s->mcount == 0 && s->count < 10)
		return 1;

	if (age > SECONDS_1D && s->mcount == 0)
		return 1;

	if (age > SECONDS_1W)
		return 1;

	return 0;
}

static int mac_expired(mac_t *m) {
	int age = AGE(m);
	int fake = EMPTY(m->ou);

	if (age > SECONDS_5M && m->count < 5 && fake)
		return 1;

	if (age > SECONDS_1H && m->count < 10 && fake)
		return 1;

	if (age > SECONDS_1D && m->count < 100)
		return 1;

	if (age > SECONDS_1W)
		return 1;

	return 0;
}

static int beacon_expired(mac_t *m) {
	return AGE(m) > SECONDS_1W;
}

static int zombie_expired(mac_t *m) {
	return AGE(m) > SECONDS_1W;
}

static int cache_expired(mac_t *m) {
	int age = AGE(m);

	if (age > SECONDS_5M && m->count < 5)
		return 1;

	if (age > SECONDS_1H)
		return 1;

	return 0;
}

// remove expired clients
static void expire() {
//	PROFILING_START

	for (small_station_t **ss = wifi.pstation; *ss; ss++) {

		// remove expired station
		if (station_expired(SS)) {
			xdebug("WIFI station %s expired, age=%d rate=%d count=%d ccount=%d", NAME(SS), AGE(SS), PPH(SS), SS->count, SS->mcount);
			SS->mac = 0;
		}

		// remove expired clients
		for (mac_t **mm = SS->pmacs; *mm; mm++)
			if (mac_expired(MM))
				MM->mac = 0;
	}

	// remove expired BEACONS
	for (mac_t **mm = beacons->pmacs; *mm; mm++)
		if (beacon_expired(MM))
			MM->mac = 0;

	// remove expired ZOMBIES
	for (mac_t **mm = zombies->pmacs; *mm; mm++)
		if (zombie_expired(MM))
			MM->mac = 0;

	// remove expired CACHE
	for (mac_t **mm = cache->pmacs; *mm; mm++)
		if (cache_expired(MM))
			MM->mac = 0;

//	PROFILING_LOG("expire")
}

// guess home station for each client
static void home() {
//	PROFILING_START

	ZERO(homes->macs);
	for (small_station_t **ss = wifi.pstation; *ss; ss++) {
		for (mac_t **mm = SS->pmacs; *mm; mm++) {

			// too less packets per hour
			int pph = PPH(MM);
			if (pph < PPH_MIN)
				continue;

			// is a station
			if (BEACONS(MM))
				continue;

			// is AVM hardware
			if (!strncmp("AVM", MM->ou, 3))
				continue;

			// track client with maximum count over all stations - assuming this is the home station
			mac_t *h = HOMES(MM);
			if (!h)
				h = mac_big(homes, MM->mac, MM->channel, MM->signal, SS->ssid, 'h');
			if (!h)
				continue;
			if (MM->count > h->count) {
				memcpy(h, MM, MAC_SIZE);
				strcpy(h->ssid, SS->ssid);
				h->tag = 'h';
			}

			// update time stamp from cache
			mac_t *c = CACHE(h);
			if (c)
				h->ts_last = c->ts_last;
		}
	}

//	PROFILING_LOG("home")
}

#include "wifi-sort.h"

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
			small_station_t *x = wifi.pstation[j];
			small_station_t *y = wifi.pstation[j + 1];
			if (y->ts_last > x->ts_last) {
				wifi.pstation[j] = y;
				wifi.pstation[j + 1] = x;
			}
		}

	// sort all station clients by count
	for (small_station_t **ss = wifi.pstation; *ss; ss++)
		sort_count_small(SS);

	// sort BEACONS by signal
	sort_signal(beacons);

	// sort ZOMBIES by age
	sort_ts(zombies);

	// sort CACHE by count
	sort_count_big(cache);

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

static int main_test() {
	mcp_init();

	char str[] = "M-PM-?M-PM->M-PM-;M-QM-^LM-PM-7M-PM->M-PM-2M-PM-0M-QM-^BM-PM-5M-PM-;";
	xlog("encoded string %s", str);
	decode_meta_utf8(str);
	xlog("decoded string %s", str);

	uint64_t mac;
	mac = string2mac("d4:ca:6e:43:a0:25");
	xlog("IEEE %012lx = %s", mac, get_ieee_ou(mac));
	mac = string2mac("d4:ca:6f:43:a0:25");
	xlog("IEEE %012lx = %s", mac, get_ieee_ou(mac));

	mac = string2mac("c6:7b:dc:17:38:d5");
	xlog("ETHERS %012lx = %s", mac, get_ethers_name(mac));
	mac = string2mac("c6:7b:dc:17:38:d6");
	xlog("ETHERS %012lx = %s", mac, get_ethers_name(mac));

	mac_t cc, *m = &cc;
	m->mac = DUMMY;
	mac2string(m->smac, m->mac);
	strcpy(m->name, "Test");
	// NOTIFY("client is back", NAME(m), "au.wav");

	m->mac = (uint64_t) 2 << 40;
	mac2string(m->smac, m->mac);
	xlog("mac 2<<40 %012lx %s", m->mac, m->smac);

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

		if (now_ts % SECONDS_1H == 0)
			evaluate();

		if (now_ts % SECONDS_1D == 0)
			store_blob(STATE SLASH WIFI_BIN, &wifi, WIFI_SIZE);
	}
}

static int init() {
	pthread_mutex_init(&lock, NULL);
	now_ts = time(NULL);

	load_ieee();
	load_ethers();
	load_blob(TMP SLASH WIFI_BIN, &wifi, WIFI_SIZE);
	sort(); // initially update all pointers

	strcpy(beacons->ssid, SSID_BEACON);
	strcpy(zombies->ssid, SSID_ZOMBIE);
	strcpy(cache->ssid, SSID_CACHE);
	strcpy(black->ssid, SSID_BLACK);
	strcpy(names->ssid, SSID_NAME);
	strcpy(homes->ssid, SSID_HOME);

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
	store_blob(TMP SLASH WIFI_BIN, &wifi, WIFI_SIZE);

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
	while ((c = getopt(argc, argv, "lpt")) != -1) {
		switch (c) {
		case 'l':
			return mcp_main(argc, argv);
		case 'p':
			return main_popen(argc, argv);
		case 't':
			return main_test();
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
