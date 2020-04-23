#include "mcp.h"

#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include "display.h"
#include "utils.h"

#ifdef WIRINGPI
#include <wiringPi.h>
#endif

#ifdef SABRE28
#include "display.h"
#include "display-menu.h"
#endif

mcp_state_t *mcp;
mcp_config_t *cfg;

int mcp_status_get(const void *p1, const void *p2) {
	// const menuconfig_t *config = p1;
	const menuitem_t *item = p2;
	xlog("mcp_status_get %i", item->index);
	switch (item->index) {
	case 1:
		return mcp->ir_active;
	default:
		return 0;
	}
}

void mcp_status_set(const void *p1, const void *p2, int value) {
	// const menuconfig_t *config = p1;
	const menuitem_t *item = p2;
	xlog("mcp_status_set %i", item->index);
	switch (item->index) {
	case 1:
		mcp->ir_active = value;
		return;
	}
}

void mcp_system_shutdown() {
	xlog("shutting down system now!");
	if (mcp->dac_power) {
		dac_power();
	}
	system("shutdown -h now");
}

void mcp_system_reboot() {
	xlog("rebooting system now!");
	if (mcp->dac_power) {
		dac_power();
	}
	system("shutdown -r now");
}

static void sig_handler(int signo) {
	xlog("MCP received signal %d", signo);
}

static void interactive() {
	struct termios new_io;
	struct termios old_io;

	printf("interactive mode, use keys UP / DOWN / ENTER; quit with 'q'\r\n");

	// set terminal into CBREAK mode
	if ((tcgetattr(STDIN_FILENO, &old_io)) == -1) {
		xlog("cannot set CBREAK");
		exit(EXIT_FAILURE);
	}

	new_io = old_io;
	new_io.c_lflag = new_io.c_lflag & ~(ECHO | ICANON);
	new_io.c_cc[VMIN] = 1;
	new_io.c_cc[VTIME] = 0;

	if ((tcsetattr(STDIN_FILENO, TCSAFLUSH, &new_io)) == -1) {
		xlog("cannot set TCSAFLUSH");
		exit(EXIT_FAILURE);
	}

	while (1) {
		int c = getchar();
		// xlog("console 0x%20x", c);

		if (c == 'q') {
			break;
		}
		if (c == 0x1b || c == 0x5b) {
			continue; // ignore
		}

		xlog("CONSOLE: distributing key 0x%02x", c);
		dac_handle(c);
	}

	// reset terminal
	tcsetattr(STDIN_FILENO, TCSANOW, &old_io);
	printf("quit\r\n");
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
#ifdef DISPLAY
	if (display_init() < 0) {
		exit(EXIT_FAILURE);
	}
#endif

	if (dac_init() < 0) {
		exit(EXIT_FAILURE);
	}

	if (mpdclient_init() < 0) {
		exit(EXIT_FAILURE);
	}

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

#ifdef LIRC_DEV
	if (lirc_init() < 0) {
		exit(EXIT_FAILURE);
	}
#endif

	xlog("all modules successfully initialized");
}

static void mcp_close() {
#ifdef LIRC_DEV
	lirc_close();
#endif

#ifdef DEVINPUT_IR
	ir_close();
#endif

#if defined(DEVINPUT_RA) || defined(DEVINPUT_RB)
	rotary_close();
#endif

	mpdclient_close();
	dac_close();

#ifdef DISPLAY
	display_close();
#endif

	xlog("all modules successfully closed");
}

int main(int argc, char **argv) {
	xlog("MCP initializing");

	cfg = malloc(sizeof(*cfg));
	memset(cfg, 0, sizeof(*cfg));

	// parse command line arguments
	int c;
	while ((c = getopt(argc, argv, "di")) != -1) {
		switch (c) {
		case 'd':
			cfg->daemonize = 1;
			break;
		case 'i':
			cfg->interactive = 1;
			break;
		}
	}

	// fork into background
	// not necessary anymore, see http://jdebp.eu/FGA/unix-daemon-design-mistakes-to-avoid.html
	if (cfg->daemonize) {
		daemonize();
	}

	// install signal handler
	if (signal(SIGINT, sig_handler) == SIG_ERR) {
		xlog("can't catch SIGINT");
		exit(EXIT_FAILURE);
	}
	if (signal(SIGTERM, sig_handler) == SIG_ERR) {
		xlog("can't catch SIGTERM");
		exit(EXIT_FAILURE);
	}
	if (signal(SIGHUP, sig_handler) == SIG_ERR) {
		xlog("can't catch SIGHUP");
		exit(EXIT_FAILURE);
	}

	// setup wiringPi
#ifdef WIRINGPI
	if (wiringPiSetup() == -1) {
		perror("Unable to start wiringPi");
		exit(EXIT_FAILURE);
	}
#endif

	// allocate global data exchange structure
	mcp = malloc(sizeof(*mcp));
	memset(mcp, 0, sizeof(*mcp));

	// initialize modules
	mcp->ir_active = 1;
	mcp_init();

	if (cfg->interactive) {
		xlog("MCP online, waiting for input");
		interactive();
	} else {
		xlog("MCP online");
		pause();
	}

	// close modules
	mcp_close();

	xlog("MCP terminated");
	xlog_close();
	return EXIT_SUCCESS;
}
