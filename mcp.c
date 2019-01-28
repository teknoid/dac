#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>

#include "mcp.h"
#include "utils.h"

#ifdef WIRINGPI
#include <wiringPi.h>
#endif

mcp_state_t *mcp;
mcp_config_t *cfg;

static void sig_handler(int signo) {
	if (signo == SIGINT || signo == SIGTERM || signo == SIGHUP) {
		xlog("MCP halt requested");

		/* close modules */

#ifdef DEVINPUT_IR
		ir_close();
#endif

#if defined(DEVINPUT_RA) || defined(DEVINPUT_RB)
		rotary_close();
#endif

#ifdef DISPLAY
		display_close();
#endif

#ifdef LIRC_RECEIVE
		lirc_close();
#endif

		mpdclient_close();
		dac_close();

		xlog("MCP terminated");
		xlog_close();
	}
}

static void daemonize() {
	pid_t pid;

	/* Fork off the parent process */
	pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}
	if (setsid() < 0) {
		exit(EXIT_FAILURE);
	}

	/* Fork off for the second time*/
	pid = fork();
	if (pid < 0) {
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* Set new file permissions, set new root, close standard file descriptors */
	umask(0);
	chdir("/");

#ifndef LIRC_RECEIVE
	// bei LIRC offen lassen !!! sonst spint lirc bei system()
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);
#endif

	xlog("MCP forked into background");
}

static void user_input() {
	struct termios new_io;
	struct termios old_io;
	struct input_event ev;

	printf("exit with 'q'\r\n");

	// set terminal into CBREAK kmode
	if ((tcgetattr(STDIN_FILENO, &old_io)) == -1) {
		exit(EXIT_FAILURE);
	}

	new_io = old_io;
	new_io.c_lflag = new_io.c_lflag & ~(ECHO | ICANON);
	new_io.c_cc[VMIN] = 1;
	new_io.c_cc[VTIME] = 0;

	if ((tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_io)) == -1) {
		exit(EXIT_FAILURE);
	}

	ev.type = EV_KEY;
	ev.time.tv_sec = 0;
	ev.time.tv_usec = 0;
	ev.value = 1;
	while (1) {
		int c = getchar();
		if (c == 'q') {
			break;
		}

		// translate to devinput KEY values
		switch (c) {
		case ' ':
			ev.code = KEY_ENTER;
			break;
		case '+':
			ev.code = KEY_VOLUMEUP;
			break;
		case '-':
			ev.code = KEY_VOLUMEDOWN;
			break;
		}

		xlog("CONSOLE: distributing key %s (0x%0x)", devinput_keyname(ev.code), ev.code);
		dac_handle(ev);
	}

	// reset terminal
	tcsetattr(STDIN_FILENO, TCSANOW, &old_io);
	printf("quit\r\n");
	sig_handler(SIGTERM);
}

void system_shutdown() {
	mcp->power = off;
	xlog("shutting down system now!");
	system("shutdown -h now");
}

int main(int argc, char **argv) {
	cfg = malloc(sizeof(*cfg));
	mcp = malloc(sizeof(*mcp));
	strcpy(mcp->artist, "");
	strcpy(mcp->title, "");
	strcpy(mcp->album, "");

	xlog("MCP initializing");

	// parse command line arguments
	int c;
	while ((c = getopt(argc, argv, "d")) != -1) {
		switch (c) {
		case 'd':
			cfg->daemonize = 1;
			break;
		}
	}

	/* install signal handler */
	if (signal(SIGHUP, sig_handler) == SIG_ERR) {
		xlog("can't catch SIGHUP");
		exit(EXIT_FAILURE);
	}
	if (signal(SIGTERM, sig_handler) == SIG_ERR) {
		xlog("can't catch SIGTERM");
		exit(EXIT_FAILURE);
	}
	if (signal(SIGINT, sig_handler) == SIG_ERR) {
		xlog("can't catch SIGINT");
		exit(EXIT_FAILURE);
	}

	/* setup wiringPi */
#ifdef WIRINGPI
	if (wiringPiSetup() == -1) {
		perror("Unable to start wiringPi");
		exit(EXIT_FAILURE);
	}
#endif

	/* initialize modules */

#ifdef DISPLAY
	if (display_init() < 0) {
		exit(EXIT_FAILURE);
	}
#endif

	if (mpdclient_init() < 0) {
		exit(EXIT_FAILURE);
	}

	if (dac_init() < 0) {
		exit(EXIT_FAILURE);
	}

#if defined(LIRC_RECEIVE) || defined(LIRC_SEND)
	if (lirc_init() < 0) {
		exit(EXIT_FAILURE);
	}
#endif

#ifdef DEVINPUT_IR
	if (ir_init() < 0) {
		exit(EXIT_FAILURE);
	}
#endif

#if defined(DEVINPUT_RA) || defined(DEVINPUT_RB)
	if (rotary_init() < 0) {
		exit(EXIT_FAILURE);
	}
#endif

	/* fork into background */
	if (cfg->daemonize) {
		daemonize();
		xlog("MCP online");
		pause();
	} else {
		xlog("MCP online, waiting for input");
		user_input();
	}

	return 0;
}
