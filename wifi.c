// gcc -DWIFI_MAIN -DMQTT_HOST=\"mqtt\" -I./include -L./lib/x86_64 -o wifi wifi.c mcp.c utils.c network.c mqtt-tx.c -lmqttc

// iw phy phy1 interface add mon1 type monitor
// ifconfig mon1 up
// tcpdump -nevi mon1 | nc tron 6666
//
// tcpdump -nevi mon1 | tee -a /ram/tcpdump.log | nc tron 6666
// while true; do for c in `seq 1 13`; do iwconfig mon1 channel $c; sleep 1s; done; done

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

#define POPEN					"/usr/bin/tcpdump -nevi mon1"
#define PORT					6666
#define DIRTY					10

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
#define SECONDS_1HX 			(60 * 60 + 300)
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
static station_t *bcache = &stations[STATIONS - 3];
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

	if (s == zombies)
		NOTIFY("New Zombie", NAME(c), "au.wav");

	if (s == bcache)
		NOTIFY("New Station", NAME(c), "au.wav");

	if ((s == ecache || s == ocache) && !EMPTY(c->name))
		NOTIFY("New Client", NAME(c), "mau3.wav");
}

static void notify_back(station_t *s, client_t *c) {
	// not for (calculated) HOMES station
	if (s == homes)
		return;

	// only after 1+ hour
	int age = now_ts - c->ts;
	if (age < SECONDS_1HX)
		return;

	xdebug("WIFI station %s client %s is back, age=%d count=%d", NAME(s), NAME(c), age, c->count);
	line_dump = 1;

	// not when blacklisted
	if (BLACK(c->mac))
		return;

	if (s == bcache) {
		NOTIFY("Station is back", NAME(c), "au.wav");
		return;
	}

	if (s == zombies) {
		NOTIFY("Zombie is back", NAME(c), "au.wav");
		return;
	}

	// not when in CACHE
	if (find(CACHE(c->mac), c->mac))
		return;

	// not for volatile anonymous clients
	if (c->count < 1000 && EMPTY(c->name))
		return;

	NOTIFY("Client is back", NAME(c), "au.wav");
}

void notify_assigned(station_t *s, client_t *z) {
	char title[128], text[128];

	// already assigned
	if (z->tag == 'a')
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
			notify_back(s, c);
			c->count++;
			c->ts = now_ts;
			if (s == zombies) {
				// update mac, smac and ou as long as zombie is unassigned
				if (c->mac != mac && c->tag != 'a') {
					if (!BLACK(c->mac))
						xdebug("WIFI updating zombie %s tag=%c old mac=%12lx new mac=%12lx ", NAME(c), c->tag, c->mac, mac);
					c->mac = mac;
					mac2string(c->smac, c->mac);
					mac2ou(c->ou, c->mac, DESCRIPTION);
				}
			} else
				// update tag on all others
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

static void check_ssid(uint64_t mac, int channel, int signal, char *ssid) {
	for (int i = 0; i < STATIONS; i++)
		if (stations[i].mac != AFFE && !strcmp(stations[i].ssid, ssid))
			return; // already known

	// create new zombie for unknown ssid
	client(zombies, mac, channel, signal, ssid, 'z');
}

static int parse(connection_t *conn) {
//	PROFILING_START

	size_t len = strlen(conn->line);
	if (len >= NETWORK_LINEBUF)
		return xerr("WIFI line length hits maximum %d", NETWORK_LINEBUF);

	conn->line[len - 1] = 0; // remove newline
	// xlog("WIFI read line %s %s", conn->ip, conn->line);

	// make a copy for line dumping after strtok()
	memcpy(conn->line_dump, conn->line, NETWORK_LINEBUF);

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
	client_t *sac = 0, *dac = 0, *rac = 0, *tac = 0;

	// update or insert BCACHE
	if (bs)
		client(bcache, bs, schannel, ssignal, ssid, 'b');

	// update or create station
	station_t *bss = station(bs, schannel, ssignal, ssid, 1);
	if (bss) {

		// assign to BSS station
		sac = client(bss, sa, cchannel, csignal, ssid, 's');
		dac = client(bss, da, cchannel, csignal, ssid, 'd');
		rac = client(bss, ra, cchannel, csignal, ssid, 'r');
		tac = client(bss, ta, cchannel, csignal, ssid, 't');

	} else {

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

	// update or insert EVEN/ODD CACHE
	if (sac && sa != bs)
		client(CACHE(sa), sa, cchannel, signal, ssid, 's');
	if (dac && da != bs)
		client(CACHE(da), da, cchannel, signal, ssid, 'd');
	if (rac && ra != bs)
		client(CACHE(ra), ra, cchannel, signal, ssid, 'r');
	if (tac && ta != bs)
		client(CACHE(ta), ta, cchannel, signal, ssid, 't');

	line_count++;
	conn->line_count++;
	if (line_dump)
		xdebug(conn->line_dump);

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
	conn->line_count++;
	conn->line[strlen(conn->line) - 1] = 0; // remove LF
	conn->line[strlen(conn->line) - 1] = 0; // remove CR
	// xdebug("WIFI command %s", conn->line);

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

	fprintf(fp, TCOMP, scount, zombies->ccount, homes->ccount, bcache->ccount, ecache->ccount, ocache->ccount, control->ccount, line_count);
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
	PROFILING_START

	xdebug(TDUMP, scount, zombies->ccount, homes->ccount, bcache->ccount, ecache->ccount, ocache->ccount, control->ccount, line_count);
	dump_compact();
	dump_flat();
	dump_station(zombies);
	dump_station(control);
	dump_station(ecache);
	dump_station(ocache);
	dump_station(bcache);
	dump_station(homes);

	PROFILING_LOG("dump")
}

// check if zombie is assigned to any station
static void assign() {
	PROFILING_START

	xdebug("assign");
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

	PROFILING_LOG("assign")
}

// remove expired clients
static void expire() {
	PROFILING_START

	xdebug("expire");
	for (station_t **ss = pstations; *ss; ss++) {

		// CONTROL never expires
		if (SS == control)
			continue;

		// remove expired station
		int age = now_ts - SS->ts;
		int ee = age > SECONDS_1D * 2;
		int e1 = SS->ccount == 0 && age > SECONDS_1D && EMPTY(SS->ssid);
		if (ee || e1) {
			xdebug("WIFI station %s expired, age=%d count=%d ccount=%d", NAME(SS), age, SS->count, SS->ccount);
			SS->mac = 0;
		}

		// remove expired clients
		for (client_t **cc = SS->pclients; *cc; cc++) {
			int keep = SS == zombies || SS == bcache;
			int fake = EMPTY(CC->ou);
			int age = now_ts - CC->ts;
			int ee = age > SECONDS_1W;
			int ec = (SS == ecache || SS == ocache) && age > SECONDS_1H;
			int e1 = !keep && CC->count < 5 && age > SECONDS_5M && fake;
			int e2 = !keep && CC->count < 10 && age > SECONDS_1H && fake;
			int e3 = !keep && CC->count < 100 && age > SECONDS_1D;
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

	PROFILING_LOG("expire")
}

// guess home station for each client
static void home() {
	PROFILING_START

	xdebug("home");
	ZERO(homes->clients);
	for (station_t **ss = pstations; *ss; ss++) {
		if (SS->mac == AFFE)
			continue;

		for (client_t **cc = SS->pclients; *cc; cc++) {

			// too less counts
			if (CC->count < 1000)
				continue;

			// is a station
			if (find(bcache, CC->mac))
				continue;

			// is AVM hardware
			if (!strncmp("AVM", CC->ou, 3))
				continue;

			// track client with maximum count over all stations - assuming this is the home station
			client_t *h = find(homes, CC->mac);
			if (!h)
				h = client(homes, CC->mac, CC->channel, CC->signal, SS->ssid, 'h');
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

	PROFILING_LOG("home")
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
	PROFILING_START

	xdebug("sort");
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

	// sort BCACHE by signal
	sort_signal(bcache);

	// sort HOMES by ssid
	sort_ssid(homes);

	pthread_mutex_unlock(&lock);
	PROFILING_LOG("sort")
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

	dump_compact();
	dump_flat();

	mcp_stop();
	return 0;
}

static void loop() {
	while (1) {
		sleep(1);
		now_ts = zombies->ts = control->ts = bcache->ts = ecache->ts = ocache->ts = homes->ts = time(NULL);
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
	now_ts = zombies->ts = control->ts = bcache->ts = ecache->ts = ocache->ts = homes->ts = time(NULL);

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

	strcpy(bcache->ssid, "BCACHE");
	bcache->mac = AFFE;
	mac2string(bcache->smac, bcache->mac);

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
