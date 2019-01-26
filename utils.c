#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "mcp.h"
#include "utils.h"
#include "keytable.h"

FILE *flog;

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
	fprintf(flog, "\n");
	fflush(flog);
}

void xlog_close() {
	if (flog) {
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
