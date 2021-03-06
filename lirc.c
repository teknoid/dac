#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <linux/input.h>

#include "mcp.h"
#include "utils.h"

static int fd_lirc_rx = -1;
static pthread_t thread_lirc;
static void *lirc(void *arg);

static int _lirc_socket(char *device) {
	struct sockaddr_un addr_un;

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) {
		xlog("could not open LIRC socket");
	}

	addr_un.sun_family = AF_UNIX;
	strcpy(addr_un.sun_path, device);
	if (connect(fd, (struct sockaddr *) &addr_un, sizeof(addr_un)) == -1) {
		xlog("could not connect to LIRC socket");
	}
	return fd;
}

void lirc_send(const char *remote, const char *command) {
	char buffer[BUFSIZE];
	int done, todo;

	int fd_lirc_tx = _lirc_socket(LIRC_DEV);
	memset(buffer, 0, BUFSIZE);
	sprintf(buffer, "SEND_ONCE %s %s\n", remote, command);
	// xlog("LIRC sending %s", buffer);
	todo = strlen(buffer);
	char *data = buffer;
	while (todo > 0) {
		done = write(fd_lirc_tx, (void *) data, todo);
		if (done < 0) {
			xlog("could not send LIRC packet");
			close(fd_lirc_tx);
			return;
		}
		data += done;
		todo -= done;
	}
	close(fd_lirc_tx);
}

int lirc_init() {
	fd_lirc_rx = _lirc_socket(LIRC_DEV);

	// listen for lirc events
	if (pthread_create(&thread_lirc, NULL, &lirc, NULL)) {
		xlog("Error creating thread_lirc");
		return -1;
	}

	xlog("LIRC initialized");
	return 0;
}

void lirc_close() {
	if (pthread_cancel(thread_lirc)) {
		xlog("Error canceling thread_lirc");
	}
	if (pthread_join(thread_lirc, NULL)) {
		xlog("Error joining thread_lirc");
	}

	if (fd_lirc_rx) {
		close(fd_lirc_rx);
	}
}

static void* lirc(void *arg) {
	int id, seq;
	char buffer[BUFSIZE], name[BUFSIZE], remote[BUFSIZE];

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	struct input_event ev;
	ev.type = EV_KEY;
	ev.time.tv_sec = 0;
	ev.time.tv_usec = 0;

	while (1) {
		memset(buffer, 0, BUFSIZE);
		int i = read(fd_lirc_rx, buffer, BUFSIZE);
		if (i == -1) {
			fd_lirc_rx = _lirc_socket(LIRC_DEV);
			xlog("could not read LIRC packet, socket reopened");
			sleep(1);
			continue;
		} else if (i == 0) {
			fd_lirc_rx = _lirc_socket(LIRC_DEV);
			xlog("LIRC: 0 read, socket reopened");
			sleep(1);
			continue;
		}

		// xlog("LIRC raw %s", buffer);

		if (sscanf(buffer, "%x %x %s %s\n", &id, &seq, name, remote) != 4) {
			continue;
		}
		if (0 < seq && seq < 4) {
			continue;
		}

		int key = devinput_find_key(name);
		if (!key) {
			continue;
		}

		ev.code = key;
		ev.value = seq; // abuse value for sequence

		dac_handle(ev.code);
	}
}
