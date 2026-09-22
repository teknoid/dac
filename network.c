#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <netdb.h>

#include "network.h"
#include "utils.h"

// cat /usr/share/ieee-data/oui.csv |sort >/usr/share/ieee-data/oui_sorted.csv
// and then manually remove last line (headline)
#define IEEE					"/usr/share/ieee-data/oui.csv"
#define IEEE_SORTED				"/usr/share/ieee-data/oui_sorted.csv"
#define IEEE_MASK				0xffffff000000

#define ETHERS					"/server/mikrotik/INSTALL/mnt/sda1/etc/dnsmasq.d/ethers"

#define TRACE_FILE				"/tmp/network.txt"

static description_t ethers[0xff];
static description_t ieee[0xffff];
static int ieee_index[0xff];

static FILE *file;

static void* popen_thread(void *arg) {
	server_t *server = (server_t*) arg;

	connection_t *conn = calloc(1, sizeof(connection_t));
	conn->stream = popen(server->command, "r");
	if (conn->stream == NULL)
		return xerrv("NETWORK popen failed");

	xlog("WIFI %d pipe opened to '%s'", server->description, server->command);
	while (!feof(conn->stream))
		if (fgets(conn->line, NETWORK_LINEBUF, conn->stream) != NULL)
			(server->handler)(conn);

	fclose(conn->stream);
	free(conn);

	pthread_exit(NULL);
}

static void* connection_thread(void *arg) {
	connection_t *conn = (connection_t*) arg;

	// convert socket into file stream for reading line by line
	conn->stream = fdopen(conn->sock, "r+");
	if (conn->stream == NULL)
		return xerrv("NETWORK fdopen failed");

	while (!feof(conn->stream))
		if (fgets(conn->line, NETWORK_LINEBUF, conn->stream) != NULL) {
			conn->line_count++;

#ifdef TRACE_FILE
			fprintf(file, conn->line);
			fflush(file);
#endif

			size_t len = strlen(conn->line);
			if (len >= NETWORK_LINEBUF - 10)
				xerr("NETWORK Warning! line length near maximum %d", NETWORK_LINEBUF);

			// remove CR and LF
			if (conn->line[strlen(conn->line) - 1] == 0x0a)
				conn->line[strlen(conn->line) - 1] = 0;
			if (conn->line[strlen(conn->line) - 1] == 0x0d)
				conn->line[strlen(conn->line) - 1] = 0;

			// make a copy for line dumping after strtok()
			memcpy(conn->copy, conn->line, NETWORK_LINEBUF);

			// xdebug("NETWORK [%s] %s", conn->ip, conn->line);

			// execute connection handler to process line
			(conn->handler)(conn);
		}

	xlog("NETWORK %s client %s disconnected, received %d lines", conn->description, conn->ip, conn->line_count);
	if (conn->stream)
		fclose(conn->stream);
	if (conn->sock)
		close(conn->sock);
	free(conn);

	pthread_exit(NULL);
}

static void* server_thread(void *arg) {
	server_t *server = (server_t*) arg;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL))
		return xerrv("NETWORK pthread_setcancelstate failed");

#ifdef TRACE_FILE
	file = fopen(TRACE_FILE, "wt");
	if (file == NULL)
		return xerrv("NETWORK error opening file %s", TRACE_FILE);
#endif

	xlog("NETWORK listening on port %d for %s", server->port, server->description);
	while (1) {
		connection_t *conn = calloc(1, sizeof(connection_t));
		conn->addr_len = sizeof(conn->address);
		conn->description = server->description;
		conn->handler = server->handler;

		// wait for client connection
		conn->sock = accept(server->sock, &conn->address, &conn->addr_len);
		if (conn->sock < 0)
			return xerrv("NETWORK accept failed");

		// get client ip address
		struct sockaddr_in *sa_in = (struct sockaddr_in*) &conn->address;
		char *ip = inet_ntoa(sa_in->sin_addr);
		strncpy(conn->ip, ip, 15);
		xlog("NETWORK new %s connection from %s", server->description, conn->ip);

		// start new thread handling this connection
		if (pthread_create(&conn->thread, 0, &connection_thread, (void*) conn))
			return xerrv("NETWORK pthread_create failed");

		// detach it
		if (pthread_detach(conn->thread))
			return xerrv("NETWORK pthread_detach failed");
	}

#ifdef TRACE_FILE
	if (file != NULL)
		fclose(file);
#endif

	pthread_exit(NULL);
}

int init_server(server_t *server, char *description, int port, handler_t handler) {
	server->port = port;
	server->handler = handler;
	server->description = description;
	server->addr_len = sizeof(server->address);

	// create server socket
	server->sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server->sock < 0)
		return xerr("NETWORK socket failed");

	int opt = 1;
	setsockopt(server->sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in *sa_in = (struct sockaddr_in*) &server->address;
	sa_in->sin_family = AF_INET;
	sa_in->sin_addr.s_addr = INADDR_ANY;
	sa_in->sin_port = htons(server->port);

	if (bind(server->sock, &server->address, server->addr_len) < 0)
		return xerr("NETWORK bind failed");

	if (listen(server->sock, SOMAXCONN) < 0)
		return xerr("NETWORK listen failed");

	if (pthread_create(&server->thread, NULL, &server_thread, (void*) server))
		return xerr("NETWORK Error creating thread");

	return 0;
}

int init_popen(server_t *local, char *description, char *command, handler_t handler) {
	local->description = description;
	local->command = command;
	local->handler = handler;

	if (pthread_create(&local->thread, NULL, &popen_thread, (void*) local))
		return xerr("NETWORK Error creating thread");

	return 0;
}

int init_socket_nb(const char *addr, const char *port) {
	struct addrinfo hints = { 0 };
	hints.ai_family = AF_UNSPEC; /* IPv4 or IPv6 */
	hints.ai_socktype = SOCK_STREAM; /* Must be TCP */

	// get address information
	struct addrinfo *p, *servinfo;
	int rv = getaddrinfo(addr, port, &hints, &servinfo);
	if (rv != 0)
		return xerr("NETWORK getaddrinfo failed: %s", gai_strerror(rv));

	// open the first possible socket
	int sock = -1;
	for (p = servinfo; p != NULL; p = p->ai_next) {
		sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sock != -1)
			break;
	}

	freeaddrinfo(servinfo);

	if (sock == -1)
		return xerr("NETWORK socket failed");

	// set keep alive
	int keepalive = 1;
	rv = setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
	if (rv == -1)
		xerr("NETWORK setsockopt SO_KEEPALIVE failed");

	// connect to server
	rv = connect(sock, p->ai_addr, p->ai_addrlen);
	if (rv == -1) {
		close(sock);
		return xerr("NETWORK connect failed");
	}

	// make non-blocking
	int flags = fcntl(sock, F_GETFL);
	rv = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
	if (rv == -1)
		xerr("NETWORK fcntl O_NONBLOCK failed");

	return sock;
}

const char* resolve_ip(const char *hostname) {
	struct addrinfo hints = { 0 };
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags |= AI_CANONNAME;

	struct addrinfo *addr;

	if (getaddrinfo(hostname, NULL, &hints, &addr) != 0)
		return xerrv("NETWORK Could not resolve inetAddr for %s", hostname);

	void *ptr = 0;
	switch (addr->ai_family) {
	case AF_INET:
		ptr = &((struct sockaddr_in*) addr->ai_addr)->sin_addr;
		break;
	case AF_INET6:
		ptr = &((struct sockaddr_in6*) addr->ai_addr)->sin6_addr;
		break;
	default:
	}

	char *addrstr = malloc(16);
	ZEROP(addrstr);

	inet_ntop(addr->ai_family, ptr, addrstr, 16);
	xlog("NETWORK %s IPv%d address: %s (%s)", hostname, addr->ai_family == PF_INET6 ? 6 : 4, addrstr, addr->ai_canonname);
	freeaddrinfo(addr);

	return addrstr;
}

const char* get_ethers_name(uint64_t mac) {
	for (int i = 0; i < 0xff; i++)
		if (ethers[i].mac == mac)
			return ethers[i].description;

	return NULL;
}

const char* get_ieee_ou(uint64_t mac) {
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
	uint64_t m = mac & IEEE_MASK;
	for (int i = from; i < to; i++)
		if (ieee[i].mac == m)
			return ieee[i].description;

	return NULL;
}

void mac2name(char *name, uint64_t mac, size_t size) {
	const char *c = get_ethers_name(mac);
	if (c != NULL)
		strncpy(name, c, size);
	else
		*name = 0;
}

void mac2ou(char *ou, uint64_t mac, size_t size) {
	const char *c = get_ieee_ou(mac);
	if (c != NULL)
		strncpy(ou, c, size);
	else
		*ou = 0;
}

uint64_t string2mac(const char *mac) {
	unsigned int u[6]; // %x needs "unsigned int"

	int c = sscanf(mac, "%x:%x:%x:%x:%x:%x", u, u + 1, u + 2, u + 3, u + 4, u + 5);
	if (c != 6)
		return 0;

	uint64_t x = 0;
	for (int i = 0; i < 6; i++)
		x = (x << 8) | (u[i] & 0xff);

	return x;
}

void mac2string(char *smac, uint64_t mac) {
	unsigned int u[6]; // %x needs "unsigned int"

	for (int i = 0; i < 6; i++) {
		u[i] = mac & 0xff;
		mac = mac >> 8;
	}

	snprintf(smac, 18, "%02x:%02x:%02x:%02x:%02x:%02x", u[5], u[4], u[3], u[2], u[1], u[0]);
}

int load_ethers() {
	char line[NETWORK_LINEBUF + 1], vv[NETWORK_LINEBUF + 1], name[NETWORK_DESCRIPTION + 1];

	ZERO(ethers);
	FILE *fp = fopen(ETHERS, "rt");
	if (fp == NULL)
		return xerr("NETWORK Cannot open file %s for reading", ETHERS);

	int ii = 0;
	while (fgets(line, NETWORK_LINEBUF, fp) != NULL) {

		// not a ether entry
		if (!starts_with("dhcp-host", line, strlen(line)))
			continue;

		// forward to values
		char *v = strchr(line, '=') + 1;

		// remove newline
		v[strlen(v) - 1] = 0;

		// copy line, then split into tokens and find name (next after mac list)
		strncpy(vv, v, NETWORK_LINEBUF);
		char *t, *rest = vv;
		while ((t = strtok_r(rest, ",", &rest))) {
			while (*t == ' ')
				t++; // trim
			if (*(t + 2) != ':' && *(t + 5) != ':' && *(t + 8) != ':')
				break; // not a mac
		}
		while (*(t + strlen(t) - 1) == '\n')
			*(t + strlen(t) - 1) = 0; // trim
		strncpy(name, t, NETWORK_DESCRIPTION);
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
				d->mac = string2mac(t);
				strncpy(d->description, name, NETWORK_DESCRIPTION);
			}
		}
	}

	fclose(fp);
	xlog("NETWORK loaded %d entries from %s", ii, ETHERS);

	// for (int i = 0; i < ii; i++)
	// xlog("%lx = %s", ethers[i].mac, ethers[i].description);

	return 0;
}

int load_ieee() {
	char line[NETWORK_LINEBUF + 1], *s, *e;

	ZERO(ieee);
	FILE *fp = fopen(IEEE_SORTED, "rt");
	if (fp == NULL)
		return xerr("NETWORK Cannot open file %s for reading", IEEE_SORTED);

	int ii = 0;
	while (fgets(line, NETWORK_LINEBUF, fp) != NULL) {

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
		strncpy(d->description, s, NETWORK_DESCRIPTION);
	}

	fclose(fp);
	xlog("NETWORK loaded %d entries from %s", ii, IEEE_SORTED);

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

void mac2name_grep(char *buf, uint64_t mac, size_t size) {
	char smac[16], cmd[128], line[1024];

	ZERO(line);
	mac2string(smac, mac);
	snprintf(cmd, 128, "grep %s /server/mikrotik/INSTALL/mnt/sda1/etc/dnsmasq.d/ethers", smac);
	FILE *fd = popen(cmd, "r");
	fgets(line, 1024, fd);
	pclose(fd);

	if (!*line)
		return;

	// forward to values
	char *v = strchr(line, '=') + 1;

	// name is next after mac
	char *t = strtok(v, ",");
	while (t != NULL) {
		while (*t == ' ')
			t++; // trim
		if (*(t + 2) != ':' && *(t + 5) != ':' && *(t + 8) != ':')
			break; // not a mac
		t = strtok(NULL, ",");
	}

	while (*(t + strlen(t) - 1) == '\n')
		*(t + strlen(t) - 1) = 0; // trim

	strncpy(buf, t, size - 1);
}
void mac2ou_grep(char *ou, uint64_t mac, size_t size) {
	char smac[16], cmd[128], line[1024];

	ZERO(line);
	snprintf(smac, 16, "%06lX", mac >> 24);
	snprintf(cmd, 128, "grep %s %s", smac, IEEE);
	FILE *fd = popen(cmd, "r");
	fgets(line, 1024, fd);
	pclose(fd);

	if (!*line)
		return;

	// Registry
	char *e, *s = strchr(line, ',');
	if (!s)
		return;

	// Assignment
	s = strchr(s + 1, ',');
	if (!s)
		return;

	// Organization Name
	s++;
	if (s[0] == '\"') {
		s++;
		e = strchr(s, '\"');
	} else
		e = strchr(s, ',');
	if (!e)
		return;

	int l = e - s;
	if (l > size)
		l = size;
	strncpy(ou, s, l - 1);
	*(ou + l) = 0;
}

