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

int _lirc = -1;

static void _lirc_socket() {
	struct sockaddr_un addr_un;

	_lirc = socket(AF_UNIX, SOCK_STREAM, 0);
	if (_lirc == -1) {
		mcplog("could not open LIRC socket");
	}

	addr_un.sun_family = AF_UNIX;
	strcpy(addr_un.sun_path, LIRC_DEV);
	if (connect(_lirc, (struct sockaddr *) &addr_un, sizeof(addr_un)) == -1) {
		mcplog("could not connect to LIRC socket");
	}
}

void lirc_send(const char *remote, const char *command) {
	char buffer[BUFSIZE];
	int done, todo;

	memset(buffer, 0, BUFSIZE);
	sprintf(buffer, "SEND_ONCE %s %s\n", remote, command);
	// mcplog("LIRC sending %s", buffer);
	todo = strlen(buffer);
	char *data = buffer;
	while (todo > 0) {
		done = write(_lirc, (void *) data, todo);
		if (done < 0) {
			mcplog("could not send LIRC packet");
			return;
		}
		data += done;
		todo -= done;
	}
	if (todo != 0) {
		_lirc_socket(); // next try
		mcplog("LIRC socket reopened");
	}
}

int lirc_init() {
	_lirc_socket();
	return 0;
}

void lirc_close() {
	if (_lirc) {
		close(_lirc);
	}
}

void* lirc(void *arg) {
	int id, seq;
	char buffer[BUFSIZE], name[BUFSIZE], remote[BUFSIZE];

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		mcplog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	while (1) {
		memset(buffer, 0, BUFSIZE);
		int i = read(_lirc, buffer, BUFSIZE);
		if (i == -1) {
			_lirc_socket();
			mcplog("could not read LIRC packet, socket reopened");
			sleep(1);
			continue;
		} else if (i == 0) {
			_lirc_socket();
			mcplog("LIRC: 0 read, socket reopened");
			sleep(1);
			continue;
		}

		// mcplog("LIRC raw %s", buffer);

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

		// mcplog("LIRC %d %d %02d %s", id, key, seq, name);
		if (key == KEY_VOLUMEUP) {
			dac_volume_up();
		} else if (key == KEY_VOLUMEDOWN) {
			dac_volume_down();
		} else if (key == KEY_POWER && seq == 0) {
			power_soft();
		} else if (key == KEY_POWER && seq == 10) {
			power_hard();
		} else if (seq == 0) {
			mcplog("LIRC: distributing key %s (0x%0x)", devinput_keyname(key), key);
			dac_handle(key);
			mpdclient_handle(key);
		}

#ifdef DISPLAY
		display_update();
#endif

	}
}
