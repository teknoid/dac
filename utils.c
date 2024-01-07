#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <time.h>
#include <netdb.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>

#include "keytable.h"
#include "utils.h"

// static int output = XLOG_STDOUT;
// static int output = XLOG_SYSLOG;
static int output = XLOG_FILE;

static const char *filename = "/var/log/mcp.log";
static FILE *xlog_file;

//
// The RT scheduler problem
//
// https://www.raspberrypi.org/forums/viewtopic.php?t=228727
// https://www.codeblueprint.co.uk/2019/10/08/isolcpus-is-deprecated-kinda.html
// https://www.iot-programmer.com/index.php/books/22-raspberry-pi-and-the-iot-in-c/chapters-raspberry-pi-and-the-iot-in-c/33-raspberry-pi-iot-in-c-almost-realtime-linux
//

int elevate_realtime(int cpu) {
	// realtime can only done by root
	if (getuid() != 0)
		return 0;

	// Set our thread to MAX priority
	struct sched_param sp;
	ZERO(&sp);
	sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
	if (sched_setscheduler(0, SCHED_FIFO, &sp))
		return -1;

	// Lock memory to ensure no swapping is done.
	if (mlockall(MCL_FUTURE | MCL_CURRENT))
		return -2;

	// pin thread to CPU
	// add this argument to /boot/cmdline.txt: isolcpus=2,3
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);
	if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset))
		return -3;

	// This permits realtime processes to use 100% of a CPU, but on a
	// RPi that starves the kernel. Without this there are latencies
	// up to 50 MILLISECONDS.
	system("echo -1 >/proc/sys/kernel/sched_rt_runtime_us");

	return 0;
}

void xlog_close() {
	if (xlog_file) {
		fflush(xlog_file);
		fclose(xlog_file);
	}

	output = XLOG_STDOUT; // switch back to stdout
}

void xlog(const char *format, ...) {
	char timestamp[256];
	va_list vargs;

	if (output == XLOG_STDOUT) {
		va_start(vargs, format);
		vprintf(format, vargs);
		va_end(vargs);
		printf("\n");
		return;
	}

	if (output == XLOG_SYSLOG) {
		va_start(vargs, format);
		vsyslog(LOG_NOTICE, format, vargs);
		va_end(vargs);
		return;
	}

	if (output == XLOG_FILE) {
		if (xlog_file == 0) {
			xlog_file = fopen(filename, "a");
			if (xlog_file == 0) {
				perror("error opening logfile!");
				exit(EXIT_FAILURE);
			}
			fprintf(xlog_file, "\nlogging initialized\n");
			fflush(xlog_file);
		}

		time_t timer;
		struct tm *tm_info;

		time(&timer);
		tm_info = localtime(&timer);
		strftime(timestamp, 26, "%d.%m.%Y %H:%M:%S", tm_info);
		fprintf(xlog_file, "%s: ", timestamp);

		va_start(vargs, format);
		vfprintf(xlog_file, format, vargs);
		va_end(vargs);
		fprintf(xlog_file, "\n");
		fflush(xlog_file);
	}
}

int xerr(const char *format, ...) {
	xlog(format);
	return -1;
}

char* printbits64(uint64_t value, uint64_t spacemask) {
	uint64_t mask = 0x8000000000000000;
	char *out = malloc(sizeof(value) * 8 * 2 + 1);
	char *p = out;
	while (mask) {
		if (value & mask)
			*p++ = '1';
		else
			*p++ = '0';

		if (mask & spacemask)
			*p++ = ' ';

		mask >>= 1;
	}
	*p++ = '\0';
	return out;
}

char* printbits32(uint32_t value, uint32_t spacemask) {
	uint32_t mask = 0x80000000;
	char *out = malloc(sizeof(value) * 8 * 2 + 1);
	char *p = out;
	while (mask) {
		if (value & mask)
			*p++ = '1';
		else
			*p++ = '0';

		if (mask & spacemask)
			*p++ = ' ';

		mask >>= 1;
	}
	*p++ = '\0';
	return out;
}

char* printbits(uint8_t value) {
	uint8_t mask = 0x80;
	char *out = malloc(sizeof(value) * 8 * 2 + 1);
	char *p = out;
	while (mask) {
		if (value & mask)
			*p++ = '1';
		else
			*p++ = '0';

		mask >>= 1;
	}
	*p++ = '\0';
	return out;
}

void hexdump(char *desc, void *addr, int len) {
	int i;
	unsigned char buff[17];
	unsigned char *pc = (unsigned char*) addr;

	// Output description if given.
	if (desc != 0)
		printf("%s:\n", desc);

	if (len == 0) {
		printf("  ZERO LENGTH\n");
		return;
	}
	if (len < 0) {
		printf("  NEGATIVE LENGTH: %i\n", len);
		return;
	}

	// Process every byte in the data.
	for (i = 0; i < len; i++) {
		// Multiple of 16 means new line (with line offset).

		if ((i % 16) == 0) {
			// Just don't print ASCII for the zeroth line.
			if (i != 0)
				printf("  %s\n", buff);

			// Output the offset.
			printf("  %04x ", i);
		}

		// Now the hex code for the specific character.
		printf(" %02x", pc[i]);

		// And store a printable ASCII character for later.
		if ((pc[i] < 0x20) || (pc[i] > 0x7e))
			buff[i % 16] = '.';
		else
			buff[i % 16] = pc[i];
		buff[(i % 16) + 1] = '\0';
	}

	// Pad out last line if not exactly 16 characters.
	while ((i % 16) != 0) {
		printf("   ");
		i++;
	}

	// And print the final ASCII bit.
	printf("  %s\n", buff);
}

int starts_with(const char *pre, const char *str, unsigned int strsize) {
	unsigned int presize = strlen(pre);
	return strsize < presize ? 0 : strncmp(pre, str, presize) == 0;
}

int ends_with(const char *post, const char *str, unsigned int strsize) {
	unsigned int postsize = strlen(post);
	return strsize < postsize ? 0 : strncmp(post, str + (strsize - postsize), postsize) == 0;
}

void create_sysfslike(char *dir, char *fname, char *fvalue, const char *fmt, ...) {
	const char *p;
	struct stat st = { 0 };
	char path[128], cp[10], *c;
	int i;
	FILE *fp;
	va_list va;

	// configured directory (remove trailing slash if necessary)
	strcpy(path, dir);
	if (path[strlen(path) - 1] == '/')
		path[strlen(path) - 1] = '\0';
	if (stat(path, &st) == -1)
		if (mkdir(path, 0755))
			perror(strerror(errno));

	// paths from varargs with format string
	va_start(va, fmt);
	for (p = fmt; *p != '\0'; p++) {
		if (*p != '%')
			continue;
		strcat(path, "/");
		switch (*++p) {
		case 'c':
			i = va_arg(va, int);
			sprintf(cp, "%c", i);
			strcat(path, cp);
			break;
		case 'd':
			i = va_arg(va, int);
			sprintf(cp, "%d", i);
			strcat(path, cp);
			break;
		case 's':
			c = va_arg(va, char*);
			strcat(path, c);
			break;
		}
		if (stat(path, &st) == -1)
			if (mkdir(path, 0755))
				perror(strerror(errno));
	}
	va_end(va);

	// file
	strcat(path, "/");
	strcat(path, fname);
	if ((fp = fopen(path, "w")) == NULL)
		perror(strerror(errno));
	while (*fvalue)
		fputc(*fvalue++, fp);
	fputc('\n', fp);
	fclose(fp);
}

char* devinput_keyname(unsigned int key) {
	struct parse_event *p;
	for (p = key_events; p->name != NULL; p++) {
		if (key == p->value) {
			return p->name;
		}
	}
	return NULL;
}

int devinput_find_key(const char *name) {
	struct parse_event *p;
	for (p = key_events; p->name != NULL; p++) {
		if (!strcmp(name, p->name)) {
			return p->value;
		}
	}
	return 0;
}

uint64_t mac2uint64(char *mac) {
	unsigned int u[6];

	int c = sscanf(mac, "%x:%x:%x:%x:%x:%x", u, u + 1, u + 2, u + 3, u + 4, u + 5);
	if (c != 6)
		return 0;

	uint64_t x = 0;
	for (int i = 0; i < 6; i++)
		x = (x << 8) | (u[i] & 0xff);

	return x;
}

char* resolve_ip(const char *hostname) {
	struct addrinfo hints = { 0 };
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags |= AI_CANONNAME;

	struct addrinfo *addr;

	if (getaddrinfo(hostname, NULL, &hints, &addr) != 0) {
		xerr("Could not resolve inetAddr for %s\n", hostname);
		return NULL;
	}

	void *ptr;
	switch (addr->ai_family) {
	case AF_INET:
		ptr = &((struct sockaddr_in*) addr->ai_addr)->sin_addr;
		break;
	case AF_INET6:
		ptr = &((struct sockaddr_in6*) addr->ai_addr)->sin6_addr;
		break;
	}

	char *addrstr = malloc(16);
	ZERO(addrstr);

	inet_ntop(addr->ai_family, ptr, addrstr, 16);
	printf("%s IPv%d address: %s (%s)\n", hostname, addr->ai_family == PF_INET6 ? 6 : 4, addrstr, addr->ai_canonname);
	freeaddrinfo(addr);

	return addrstr;
}

