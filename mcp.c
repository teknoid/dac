#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <sys/stat.h>

#include "display.h"
#include "button.h"
#include "utils.h"
#include "mqtt.h"
#include "lcd.h"
#include "dac.h"
#include "mcp.h"

#if defined(SABRE18) || defined(SABRE28)
#include "display-menu.h"
#endif

mcp_state_t *mcp = NULL;
mcp_config_t *cfg = NULL;
mcp_modules_t *modules = NULL;

// register a new module in the module chain
// called in each module via macro MCP_REGISTER(...) before main()
void mcp_register(const char *name, const void *init, const void *destroy) {
	mcp_modules_t *new_mod = malloc(sizeof(mcp_modules_t));
	memset(new_mod, 0, sizeof(mcp_modules_t));

	new_mod->name = name;
	new_mod->init = init;
	new_mod->destroy = destroy;
	new_mod->next = NULL;

	if (modules == NULL)
		// this is the head
		modules = new_mod;
	else {
		// append to last in chain
		mcp_modules_t *mod = modules;
		while (mod->next != NULL)
			mod = mod->next;
		mod->next = new_mod;
	}

	xlog("MCP registered module %s", name);
}

int mcp_status_get(const void *p1, const void *p2) {
#if defined(SABRE18) || defined(SABRE28)
	// const menuconfig_t *config = p1;
	const menuitem_t *item = p2;
	xlog("mcp_status_get %i", item->index);
	switch (item->index) {
	case 1:
		return mcp->ir_active;
	default:
		return 0;
	}
#else
	return 0;
#endif
}

void mcp_status_set(const void *p1, const void *p2, int value) {
#if defined(SABRE18) || defined(SABRE28)
	// const menuconfig_t *config = p1;
	const menuitem_t *item = p2;
	xlog("mcp_status_set %i", item->index);
	switch (item->index) {
	case 1:
		mcp->ir_active = value;
		return;
	}
#endif
}

void mcp_system_shutdown() {
	xlog("shutting down system now!");
	if (mcp->dac_power)
		dac_power();
	system("shutdown -h now");
}

void mcp_system_reboot() {
	xlog("rebooting system now!");
	if (mcp->dac_power)
		dac_power();
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

		if (c == 'q')
			break;

		if (c == 0x1b || c == 0x5b)
			continue; // ignore

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
	if (pid < 0)
		exit(EXIT_FAILURE);

	if (pid > 0)
		exit(EXIT_SUCCESS);

	if (setsid() < 0)
		exit(EXIT_FAILURE);

	/* Catch, ignore and handle signals */
	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* Fork off for the second time*/
	pid = fork();
	if (pid < 0)
		exit(EXIT_FAILURE);

	if (pid > 0)
		exit(EXIT_SUCCESS);

	/* Set new file permissions, set new root, close standard file descriptors */
	umask(0);
	chdir("/");

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	xlog("MCP forked into background");
}

// loop over module chain and call each module's init() function
static void init() {
	mcp_modules_t *module = modules;
	while (1) {
		if ((module->init)() < 0)
			exit(EXIT_FAILURE);

		if (module->next == NULL)
			break;
		else
			module = module->next;
	}
	xlog("all modules initialized");
}

// loop over module chain and call each module's destroy() function
static void destroy() {
	mcp_modules_t *module = modules;
	while (1) {
		(module->destroy)();

		if (module->next == NULL)
			break;
		else
			module = module->next;
	};
	xlog("all modules destroyed");
}

int main(int argc, char **argv) {
	xlog_init(XLOG_SYSLOG, NULL);
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
	if (cfg->daemonize)
		daemonize();

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

	// allocate global data exchange structure
	mcp = malloc(sizeof(*mcp));
	memset(mcp, 0, sizeof(*mcp));

	// initialize all registered modules
	mcp->ir_active = 1;
	init();

	if (cfg->interactive) {
		xlog("MCP online, waiting for input");
		interactive();
	} else {
		xlog("MCP online");
		pause();
	}

	// destroy all registered modules
	destroy();

	xlog("MCP terminated");
	xlog_close();
	return EXIT_SUCCESS;
}
