#define _GNU_SOURCE

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "utils.h"
#include "keytable.h"
#include "mcp.h"

static FILE *flog;

#include "utils.h"

static volatile unsigned long *systReg = 0;

//
// The RT scheduler problem
//
// https://www.raspberrypi.org/forums/viewtopic.php?t=228727
// https://www.codeblueprint.co.uk/2019/10/08/isolcpus-is-deprecated-kinda.html
// https://www.iot-programmer.com/index.php/books/22-raspberry-pi-and-the-iot-in-c/chapters-raspberry-pi-and-the-iot-in-c/33-raspberry-pi-iot-in-c-almost-realtime-linux
//

int init_micros() {
	// based on pigpio source; simplified and re-arranged
	int fdMem = open("/dev/mem", O_RDWR | O_SYNC);
	if (fdMem < 0) {
		perror("Cannot map memory (need sudo?)\n");
		return -1;
	}
	// figure out the address
	FILE *f = fopen("/proc/cpuinfo", "r");
	char buf[1024];
	fgets(buf, sizeof(buf), f); // skip first line
	fgets(buf, sizeof(buf), f); // model name
	unsigned long phys = 0;
	if (strstr(buf, "ARMv6")) {
		phys = 0x20000000;
	} else if (strstr(buf, "ARMv7")) {
		phys = 0x3F000000;
	} else if (strstr(buf, "ARMv8")) {
		phys = 0x3F000000;
	} else {
		perror("Unknown CPU type\n");
		return -1;
	}
	fclose(f);
	systReg = (unsigned long *) mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fdMem, phys + 0x3000);
	return 0;
}

unsigned long _micros() {
	return systReg[1];
}

void delay_micros(unsigned int us) {
	// usleep() on its own gives latencies 20-40 us; this combination gives < 25 us.
	unsigned long start = systReg[1];
	if (us >= 100)
		usleep(us - 50);
	while ((systReg[1] - start) < us)
		;
}

int elevate_realtime(int cpu) {
	// Set our thread to MAX priority
	struct sched_param sp;
	memset(&sp, 0, sizeof(sp));
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

void xlog(char *format, ...) {
	va_list vargs;
	time_t timer;
	char buffer[26];
	struct tm* tm_info;

	if (flog == 0) {
		flog = fopen(LOGFILE, "a");
		if (flog == 0) {
			perror("error opening logfile " LOGFILE);
			exit(EXIT_FAILURE);
		}
	}

	time(&timer);
	tm_info = localtime(&timer);
	strftime(buffer, 26, "%d.%m.%Y %H:%M:%S", tm_info);

	fprintf(flog, "%s: ", buffer);
	va_start(vargs, format);
	vfprintf(flog, format, vargs);
	va_end(vargs);
	fprintf(flog, "\r\n");
	fflush(flog);
}

void xlog_close() {
	if (flog) {
		fflush(flog);
		fclose(flog);
	}
}

int startsWith(const char *pre, const char *str) {
	unsigned int lenpre = strlen(pre);
	unsigned int lenstr = strlen(str);
	return lenstr < lenpre ? 0 : strncmp(pre, str, lenpre) == 0;
}

char *printBits(char value) {
	char *out = malloc(sizeof(char) * 8) + 1;
	char *p = out;
	for (unsigned char mask = 0b10000000; mask > 0; mask >>= 1) {
		if (value & mask) {
			*p++ = '1';
		} else {
			*p++ = '0';
		}
	}
	*p++ = '\0';
	return out;
}

void hexDump(char *desc, void *addr, int len) {
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

char *devinput_keyname(unsigned int key) {
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
