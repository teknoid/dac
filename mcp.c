#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <signal.h>
#include <string.h>
#include <time.h>
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
	if (power_init() < 0) {
		exit(EXIT_FAILURE);
	}

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
	}

	xlog("MCP online");
	pause();
	return 0;
}
