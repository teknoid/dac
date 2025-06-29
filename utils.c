#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <syslog.h>
#include <time.h>
#include <netdb.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>

#include <ansi-color-codes.h>
#include <keytable.h>

#include "utils.h"

// static int output = XLOG_STDOUT;
// static int output = XLOG_SYSLOG;
static int output = XLOG_FILE;

static int debug = 0;

static const char *filename = "/var/log/mcp.log";
static FILE *xlog_file;

static pthread_mutex_t lock;

static void xlog_open() {
	xlog_file = fopen(filename, "at");
	if (xlog_file == 0) {
		perror("error opening logfile!");
		exit(EXIT_FAILURE);
	}

	pthread_mutex_init(&lock, NULL);
	fprintf(xlog_file, "\nlogging initialized\n");
	fflush(xlog_file);
}

void set_debug(int d) {
	debug = d;
}

void set_xlog(int o) {
	output = o;
}

void xlog_close() {
	if (xlog_file) {
		fflush(xlog_file);
		fclose(xlog_file);
	}

	pthread_mutex_destroy(&lock);
	output = XLOG_STDOUT; // switch back to stdout
}

void xlog(const char *format, ...) {
	if (!output)
		return;

	va_list vargs;

	pthread_mutex_lock(&lock);

	if (output == XLOG_STDOUT) {
		va_start(vargs, format);
		vprintf(format, vargs);
		va_end(vargs);
		printf("\n");
	}

	if (output == XLOG_SYSLOG) {
		va_start(vargs, format);
		vsyslog(LOG_NOTICE, format, vargs);
		va_end(vargs);
	}

	if (output == XLOG_FILE) {
		if (xlog_file == 0)
			xlog_open();

		LOCALTIME

		char timestamp[26];
		strftime(timestamp, 26, "%d.%m.%Y %H:%M:%S", now);
		fprintf(xlog_file, "%s ", timestamp);

		va_start(vargs, format);
		vfprintf(xlog_file, format, vargs);
		va_end(vargs);
		fprintf(xlog_file, "\n");
		fflush(xlog_file);
	}

	pthread_mutex_unlock(&lock);
}

void xdebug(const char *format, ...) {
	if (!output || !debug)
		return;

	// !!! do not call xlog(format) here as varargs won't work correctly
	va_list vargs;

	pthread_mutex_lock(&lock);

	if (output == XLOG_STDOUT) {
		va_start(vargs, format);
		vprintf(format, vargs);
		va_end(vargs);
		printf("\n");
	}

	if (output == XLOG_SYSLOG) {
		va_start(vargs, format);
		vsyslog(LOG_NOTICE, format, vargs);
		va_end(vargs);
	}

	if (output == XLOG_FILE) {
		if (xlog_file == 0)
			xlog_open();

		LOCALTIME

		char timestamp[26];
		strftime(timestamp, 26, "%d.%m.%Y %H:%M:%S", now);
		fprintf(xlog_file, "%s ", timestamp);

		va_start(vargs, format);
		vfprintf(xlog_file, format, vargs);
		va_end(vargs);
		fprintf(xlog_file, "\n");
		fflush(xlog_file);
	}

	pthread_mutex_unlock(&lock);
}

int xerr(const char *format, ...) {
	if (!output)
		return -1;

	// !!! do not call xlog(format) here as varargs won't work correctly
	va_list vargs;

	pthread_mutex_lock(&lock);

	if (output == XLOG_STDOUT) {
		va_start(vargs, format);
		vprintf(format, vargs);
		va_end(vargs);
		printf("\n");
	}

	if (output == XLOG_SYSLOG) {
		va_start(vargs, format);
		vsyslog(LOG_NOTICE, format, vargs);
		va_end(vargs);
	}

	if (output == XLOG_FILE) {
		if (xlog_file == 0)
			xlog_open();

		LOCALTIME

		char timestamp[26];
		strftime(timestamp, 26, "%d.%m.%Y %H:%M:%S", now);
		fprintf(xlog_file, "%s ", timestamp);

		va_start(vargs, format);
		vfprintf(xlog_file, format, vargs);
		va_end(vargs);
		fprintf(xlog_file, "\n");
		fflush(xlog_file);
	}

	pthread_mutex_unlock(&lock);
	return -1;
}

int xerrr(int ret, const char *format, ...) {
	if (!output)
		return ret;

	// !!! do not call xlog(format) here as varargs won't work correctly
	va_list vargs;

	pthread_mutex_lock(&lock);

	if (output == XLOG_STDOUT) {
		va_start(vargs, format);
		vprintf(format, vargs);
		va_end(vargs);
		printf("\n");
	}

	if (output == XLOG_SYSLOG) {
		va_start(vargs, format);
		vsyslog(LOG_NOTICE, format, vargs);
		va_end(vargs);
	}

	if (output == XLOG_FILE) {
		if (xlog_file == 0)
			xlog_open();

		LOCALTIME

		char timestamp[26];
		strftime(timestamp, 26, "%d.%m.%Y %H:%M:%S", now);
		fprintf(xlog_file, "%s ", timestamp);

		va_start(vargs, format);
		vfprintf(xlog_file, format, vargs);
		va_end(vargs);
		fprintf(xlog_file, "\n");
		fflush(xlog_file);
	}

	pthread_mutex_unlock(&lock);
	return ret;
}

void xlogl_start(char *line, const char *s) {
	if (s != NULL)
		strncpy(line, s, LINEBUF);
	else
		line[0] = '\0';
}

void xlogl_bits(char *line, const char *name, int bits) {
	char c[32];
	snprintf(c, 32, " %s:"BYTE2BIN_PATTERN, name, BYTE2BIN(bits));
	strncat(line, c, 32);
}

void xlogl_bits16(char *line, const char *name, int bits) {
	char c[32];
	snprintf(c, 32, " %s:"BYTE2BIN_PATTERN16, name, BYTE2BIN16(bits));
	strncat(line, c, 32);
}

void xlogl_float(char *line, const char *name, float value) {
	char pair[32];
	snprintf(pair, 32, " %s:%.1f", name, value);
	strncat(line, pair, 32);
}

void xlogl_float_b(char *line, const char *name, float value) {
	char pair[32];
	snprintf(pair, 32, " "BOLD"%s:"BBLU"%.1f"RESET, name, value);
	strncat(line, pair, 32);
}

void xlogl_float_noise(char *line, float noise, int invers, const char *name, float value) {
	char pair[32];
	if (noise > 0.0) {
		// compare against noise
		if (invers) {
			if (value <= noise * -1.0)
				snprintf(pair, 32, " "BOLD"%s:"BGRN"%.1f"RESET, name, value);
			else if (value >= noise)
				snprintf(pair, 32, " "BOLD"%s:"BRED"%.1f"RESET, name, value);
			else
				snprintf(pair, 32, " "BOLD"%s:%.2f"RESET, name, value);
		} else {
			if (value >= noise)
				snprintf(pair, 32, " "BOLD"%s:"BGRN"%.1f"RESET, name, value);
			else if (value <= noise * -1.0)
				snprintf(pair, 32, " "BOLD"%s:"BRED"%.1f"RESET, name, value);
			else
				snprintf(pair, 32, " "BOLD"%s:%.1f"RESET, name, value);
		}
	} else {
		// compare against zero
		if (invers) {
			if (value < 0.0)
				snprintf(pair, 32, " "BOLD"%s:"BGRN"%.1f"RESET, name, value);
			else if (value > 0)
				snprintf(pair, 32, " "BOLD"%s:"BRED"%.1f"RESET, name, value);
			else
				snprintf(pair, 32, " "BOLD"%s:%.1f"RESET, name, value);
		} else {
			if (value > 0.0)
				snprintf(pair, 32, " "BOLD"%s:"BGRN"%.1f"RESET, name, value);
			else if (value < 0)
				snprintf(pair, 32, " "BOLD"%s:"BRED"%.1f"RESET, name, value);
			else
				snprintf(pair, 32, " "BOLD"%s:%.1f"RESET, name, value);
		}
	}
	strncat(line, pair, 32);
}

void xlogl_percent10(char *line, const char *name, int value) {
	char pair[32];
	if (value == 0)
		snprintf(pair, 32, " "BOLD"%s:0"RESET, name);
	else if (value < 900)
		snprintf(pair, 32, " "BOLD"%s:"BRED"%.1f"RESET, name, FLOAT10(value));
	else if (value > 1000)
		snprintf(pair, 32, " "BOLD"%s:"BGRN"%.1f"RESET, name, FLOAT10(value));
	else
		snprintf(pair, 32, " "BOLD"%s:%.1f"RESET, name, FLOAT10(value));
	strncat(line, pair, 32);
}

void xlogl_int(char *line, const char *name, int value) {
	char pair[32];
	snprintf(pair, 32, " %s:%d", name, value);
	strncat(line, pair, 32);
}

void xlogl_int_r(char *line, const char *name, int value) {
	char pair[32];
	snprintf(pair, 32, " "BOLD"%s:"BRED"%d"RESET, name, value);
	strncat(line, pair, 32);
}

void xlogl_int_y(char *line, const char *name, int value) {
	char pair[32];
	snprintf(pair, 32, " "BOLD"%s:"BYEL"%d"RESET, name, value);
	strncat(line, pair, 32);
}

void xlogl_int_g(char *line, const char *name, int value) {
	char pair[32];
	snprintf(pair, 32, " "BOLD"%s:"BGRN"%d"RESET, name, value);
	strncat(line, pair, 32);
}

void xlogl_int_b(char *line, const char *name, int value) {
	char pair[32];
	snprintf(pair, 32, " "BOLD"%s:"BBLU"%d"RESET, name, value);
	strncat(line, pair, 32);
}

void xlogl_int_B(char *line, const char *name, int value) {
	char pair[32];
	snprintf(pair, 32, " "BOLD"%s:%d"RESET, name, value);
	strncat(line, pair, 32);
}

void xlogl_int_noise(char *line, int noise, int invers, const char *name, int value) {
	char pair[32];
	if (noise) {
		// compare against noise
		if (invers) {
			if (value <= noise * -1)
				snprintf(pair, 32, " "BOLD"%s:"BGRN"%d"RESET, name, value);
			else if (value >= noise)
				snprintf(pair, 32, " "BOLD"%s:"BRED"%d"RESET, name, value);
			else
				snprintf(pair, 32, " "BOLD"%s:%d"RESET, name, value);
		} else {
			if (value >= noise)
				snprintf(pair, 32, " "BOLD"%s:"BGRN"%d"RESET, name, value);
			else if (value <= noise * -1)
				snprintf(pair, 32, " "BOLD"%s:"BRED"%d"RESET, name, value);
			else
				snprintf(pair, 32, " "BOLD"%s:%d"RESET, name, value);
		}
	} else {
		// compare against zero
		if (invers) {
			if (value < 0)
				snprintf(pair, 32, " "BOLD"%s:"BGRN"%d"RESET, name, value);
			else if (value > 0)
				snprintf(pair, 32, " "BOLD"%s:"BRED"%d"RESET, name, value);
			else
				snprintf(pair, 32, " "BOLD"%s:%d"RESET, name, value);
		} else {
			if (value > 0)
				snprintf(pair, 32, " "BOLD"%s:"BGRN"%d"RESET, name, value);
			else if (value < 0)
				snprintf(pair, 32, " "BOLD"%s:"BRED"%d"RESET, name, value);
			else
				snprintf(pair, 32, " "BOLD"%s:%d"RESET, name, value);
		}
	}
	strncat(line, pair, 32);
}

void xlogl_end(char *line, size_t len, const char *s) {
	if (s != NULL) {
		strcat(line, " ");
		strncat(line, s, LINEBUF);
	}

	int l = strlen(line);
	if (l > len)
		xerr("UTILS !!! Warning !!! segfault approaching due to line buffer is too small: strlen %d > sizeof %d", l, len);

	xlog(line);
}

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
	ZEROP(&sp);
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

char* make_string(const char *c, size_t s) {
	char *str = (char*) malloc(s + 1);
	memcpy(str, c, s);
	str[s] = '\0';
	return str;
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
		default:
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

uint64_t mac2uint64(const char *mac) {
	unsigned int u[6]; // %x needs "unsigned int"

	int c = sscanf(mac, "%x:%x:%x:%x:%x:%x", u, u + 1, u + 2, u + 3, u + 4, u + 5);
	if (c != 6)
		return 0;

	uint64_t x = 0;
	for (int i = 0; i < 6; i++)
		x = (x << 8) | (u[i] & 0xff);

	return x;
}

const char* resolve_ip(const char *hostname) {
	struct addrinfo hints = { 0 };
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_protocol = IPPROTO_UDP;
	hints.ai_flags |= AI_CANONNAME;

	struct addrinfo *addr;

	if (getaddrinfo(hostname, NULL, &hints, &addr) != 0) {
		xlog("UTILS Could not resolve inetAddr for %s", hostname);
		return NULL;
	}

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
	xlog("UTILS %s IPv%d address: %s (%s)", hostname, addr->ai_family == PF_INET6 ? 6 : 4, addrstr, addr->ai_canonname);
	freeaddrinfo(addr);

	return addrstr;
}

int round10(int n) {
	int x = n / 10;
	int y = n % 10;

	if (y < 5)
		return x * 10;
	else
		return (x + 1) * 10;
}

int round100(int n) {
	int x = n / 100;
	int y = n % 100;

	if (y < 5)
		return x * 100;
	else
		return (x + 1) * 100;
}

int maximum(int count, ...) {
	int max = 0;
	int i;

	va_list valist;
	va_start(valist, count);
	for (i = 0; i < count; i++) {
		int x = va_arg(valist, int);
		if (x > max)
			max = x;
	}
	va_end(valist);

	return max;
}

int average_non_zero(int array[], size_t size) {
	int sum = 0, count = 0;
	for (int i = 0; i < size; i++)
		if (array[i]) { // sum up only non zero values
			sum += array[i];
			count++;
		}
	return count == 0 ? 0 : sum / count;
}

void append_timeframe(char *message, int sec) {
	int h, m, s;
	char c[30];

	h = (sec / 3600);
	m = (sec - (3600 * h)) / 60;
	s = (sec - (3600 * h) - (m * 60));

	if (h == 0 && m == 0)
		snprintf(c, 30, "%02d", s);
	else if (h == 0)
		snprintf(c, 30, "%02d:%02d", m, s);
	else
		snprintf(c, 30, "%02d:%02d:%02d", h, m, s);

	strcat(message, c);
}

int load_blob(const char *filename, void *data, size_t size) {
	memset(data, 0, size); // zero content
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL)
		return xerr("UTILS Cannot open file %s for reading", filename);
	size_t count = fread(data, size, 1, fp);
	fclose(fp);
	xlog("UTILS loaded %5d bytes from %s", count * size, filename);
	return 0;
}

int store_blob(const char *filename, void *data, size_t size) {
	FILE *fp = fopen(filename, "wb");
	if (fp == NULL)
		return xerr("UTILS Cannot open file %s for writing", filename);
	size_t count = fwrite(data, size, 1, fp);
	fflush(fp);
	fclose(fp);
	xlog("UTILS stored %5d bytes to %s", count * size, filename);
	return 0;
}

int store_blob_offset(const char *filename, void *data, size_t rsize, int count, int offset) {
	FILE *fp = fopen(filename, "wb");
	if (fp == NULL)
		return xerr("UTILS Cannot open file %s for writing", filename);

	// xdebug("UTILS rsize=%d count=%d offset=%d", rsize, count, offset);

	// part1: from offset to end
	int ret = 0;
	int records = count - offset;
	int start = offset * rsize;
	// xdebug("UTILS part1: records=%d start=%d", records, start);
	ret += fwrite(data + start, rsize, records, fp);
	// xdebug("UTILS part1: count=%d", count);

	// part2: from start to offset
	// xdebug("UTILS part2: offset=%d", offset);
	ret += fwrite(data, rsize, offset, fp);
	// xdebug("UTILS part1: count=%d", count);

	fflush(fp);
	fclose(fp);
	xlog("UTILS stored %5d bytes to %s", ret * rsize, filename);
	return 0;
}

void aggregate(int *target, int *table, int cols, int rows) {
	memset(target, 0, sizeof(int) * cols);
	for (int x = 0; x < cols; x++) {
		int count = 0;
		for (int y = 0; y < rows; y++) {
			int *i = table + y * cols + x;
			if (*i) { // ignore 0
				target[x] += *i;
				count++;
			}
		}
		if (count) {
			int z = (target[x] * 10) / count;
			target[x] = z / 10 + (z % 10 < 5 ? 0 : 1);
		}
	}
}

void cumulate(int *target, int *table, int cols, int rows) {
	memset(target, 0, sizeof(int) * cols);
	for (int x = 0; x < cols; x++)
		for (int y = 0; y < rows; y++) {
			int *i = table + y * cols + x;
			target[x] += *i;
		}
}

void store_csv_header(const char *header, const char *filename) {
	FILE *fp = fopen(filename, "wt");
	if (fp == NULL) {
		xerr("UTILS Cannot open file %s for writing", filename);
		return;
	}

	fprintf(fp, " i%s\n", header);
	fflush(fp);
	fclose(fp);
	xlog("UTILS stored header %s", filename);
}

void store_array_csv(int array[], int size, int duplicate, const char *header, const char *filename) {
	char c[8 + 16], v[16];

	FILE *fp = fopen(filename, "wt");
	if (fp == NULL) {
		xerr("UTILS Cannot open file %s for writing", filename);
		return;
	}

	if (header)
		fprintf(fp, " i%s\n", header);

	for (int y = 0; y < size; y++) {
		snprintf(v, 16, "%02d ", y);
		strcpy(c, v);
		snprintf(v, 8, "%5d ", array[y]);
		strcat(c, v);
		fprintf(fp, "%s\n", c);
	}

	// gnuplot workaround - duplicate index 0 at the end
	if (duplicate) {
		snprintf(v, 16, "%02d ", size);
		strcpy(c, v);
		snprintf(v, 8, "%5d ", array[0]);
		strcat(c, v);
		fprintf(fp, "%s\n", c);
	}

	fflush(fp);
	fclose(fp);
	xlog("UTILS stored %s", filename);
}

void store_table_csv(int *table, int cols, int rows, const char *header, const char *filename) {
	char c[cols * 8 + 16], v[16];

	FILE *fp = fopen(filename, "wt");
	if (fp == NULL) {
		xerr("UTILS Cannot open file %s for writing", filename);
		return;
	}

	if (header)
		fprintf(fp, " i%s\n", header);

	for (int y = 0; y < rows; y++) {
		snprintf(v, 16, "%02d ", y);
		strcpy(c, v);
		for (int x = 0; x < cols; x++) {
			snprintf(v, 8, "%5d ", *table++);
			strcat(c, v);
		}
		fprintf(fp, "%s\n", c);
	}

	fflush(fp);
	fclose(fp);
	xlog("UTILS stored %s", filename);
}

void append_table_csv(int *table, int cols, int rows, int offset, const char *filename) {
	char c[cols * 8 + 16], v[16];

	FILE *fp = fopen(filename, "at");
	if (fp == NULL) {
		xerr("UTILS Cannot open file %s for writing", filename);
		return;
	}

	for (int y = 0; y < rows; y++) {
		snprintf(v, 16, "%02d ", offset + y);
		strcpy(c, v);
		for (int x = 0; x < cols; x++) {
			snprintf(v, 8, "%5d ", *table++);
			strcat(c, v);
		}
		fprintf(fp, "%s\n", c);
	}

	fflush(fp);
	fclose(fp);
	xlog("UTILS appended %s", filename);
}

void dump_table(int *table, int cols, int rows, int highlight_row, const char *title, const char *header) {
	char c[cols * 8 + 16], v[16];

	if (title)
		xdebug(title);

	if (header) {
		strcpy(c, " idx");
		strcat(c, header);
		xdebug(c);
	}

	for (int y = 0; y < rows; y++) {
		snprintf(v, 16, "[%02d] ", y);
		strcpy(c, v);
		if (y == highlight_row)
			strcat(c, BOLD);
		for (int x = 0; x < cols; x++) {
			// int *i = table + yy * x + xx;
			snprintf(v, 8, "%5d ", *table++);
			strcat(c, v);
		}
		if (y == highlight_row)
			strcat(c, RESET);
		xdebug(c);
	}
}

void dump_struct(int *values, int size, const char *idx, const char *title) {
	char c[size * 8 + 16], v[16];

	if (title)
		xdebug(title);

	snprintf(v, 16, "%s ", idx);
	strcpy(c, v);
	for (int xx = 0; xx < size; xx++) {
		snprintf(v, 8, "%5d ", values[xx]);
		strcat(c, v);
	}
	xdebug(c);
}

void store_struct_json(int *values, int size, const char *header, const char *filename) {
	FILE *fp = fopen(filename, "wt");
	if (fp == NULL) {
		xerr("UTILS Cannot open file %s for writing", filename);
		return;
	}

	int i = 0;
	char *str = strdup(header); // strtok() needs write access to the string(!)
	char *p = strtok(str, " ");

	fprintf(fp, "{");
	while (p != NULL && i < size) {
		fprintf(fp, "\"%s\":\"%d\"", p, values[i]);
		if (i != size - 1)
			fprintf(fp, ", ");
		p = strtok(NULL, " ");
		i++;
	}
	fprintf(fp, "}");

	fflush(fp);
	fclose(fp);
	free(str);
	//xdebug("UTILS stored %s", filename);
}
