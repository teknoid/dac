#include "mcp.h"

#include <getopt.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#include "display.h"
#include "utils.h"

#ifdef WIRINGPI
#include <wiringPi.h>
#endif

mcp_state_t *mcp;
mcp_config_t *cfg;

static void sig_handler(int signo) {
	xlog("MCP received signal %d", signo);
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

	/* Catch, ignore and handle signals */
	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

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

static void mcp_init() {
	if (dac_init() < 0) {
		exit(EXIT_FAILURE);
	}

	if (mpdclient_init() < 0) {
		exit(EXIT_FAILURE);
	}

#ifdef DISPLAY
	if (display_init() < 0) {
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

#if defined(LIRC_RECEIVE) || defined(LIRC_SEND)
	if (lirc_init() < 0) {
		exit(EXIT_FAILURE);
	}
#endif

	xlog("all modules successfully initialized");
}

static void mcp_close() {
#ifdef LIRC_RECEIVE
	lirc_close();
#endif

#ifdef DEVINPUT_IR
	ir_close();
#endif

#if defined(DEVINPUT_RA) || defined(DEVINPUT_RB)
	rotary_close();
#endif

#ifdef DISPLAY
	display_close();
#endif

	mpdclient_close();
	dac_close();

	xlog("all modules successfully closed");
}

static void mcp_input() {
	struct termios new_io;
	struct termios old_io;
	struct input_event ev;

	printf("quit with 'q'\r\n");

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
		dac_handle(ev.code);
	}

	// reset terminal
	tcsetattr(STDIN_FILENO, TCSANOW, &old_io);
	printf("quit\r\n");
}

void system_shutdown() {
	xlog("shutting down system now!");
	system("shutdown -h now");
}

void system_reboot() {
	xlog("rebooting system now!");
	system("shutdown -r now");
}

int main(int argc, char **argv) {
	xlog("MCP initializing");

	cfg = malloc(sizeof(*cfg));
	memset(cfg, 0, sizeof *cfg);

	// parse command line arguments
	int c;
	while ((c = getopt(argc, argv, "d")) != -1) {
		switch (c) {
		case 'd':
			cfg->daemonize = 1;
			break;
		}
	}

	/* fork into background */
	if (cfg->daemonize) {
		daemonize();
	}

	mcp = malloc(sizeof(*mcp));
	memset(mcp, 0, sizeof *mcp);

	/* setup wiringPi */
#ifdef WIRINGPI
	if (wiringPiSetup() == -1) {
		perror("Unable to start wiringPi");
		exit(EXIT_FAILURE);
	}
#endif

	/* initialize modules */
	mcp_init();

	if (cfg->daemonize) {
		/* install signal handler */
		if (signal(SIGTERM, sig_handler) == SIG_ERR) {
			xlog("can't catch SIGTERM");
			exit(EXIT_FAILURE);
		}
		xlog("MCP online");
		pause();
	} else {
		xlog("MCP online, waiting for input");
		mcp_input();
	}

	/* close modules */
	mcp_close();

	xlog("MCP terminated");
	xlog_close();
	return EXIT_SUCCESS;
}
