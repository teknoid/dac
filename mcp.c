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

#ifdef WIRINGPI
#include <wiringPi.h>
#endif

#ifdef LIRC_RECEIVE
pthread_t thread_lirc;
#endif

#ifdef DEVINPUT
pthread_t thread_devinput;
#endif

pthread_t thread_mpdclient;
pthread_t thread_dac;

FILE *flog;

void mcplog(char *format, ...) {
	va_list vargs;
	time_t timer;
	char buffer[26];
	struct tm* tm_info;

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

static void sig_handler(int signo) {
	if (signo == SIGINT || signo == SIGTERM || signo == SIGHUP) {

#ifdef LIRC_RECEIVE
		if (pthread_cancel(thread_lirc)) {
			mcplog("Error canceling thread_lirc");
		}
#endif

#ifdef DEVINPUT
		if (pthread_cancel(thread_devinput)) {
			mcplog("Error canceling thread_devinput");
		}
#endif

		if (pthread_cancel(thread_mpdclient)) {
			mcplog("Error canceling thread_mpdclient");
		}

		if (pthread_cancel(thread_dac)) {
			mcplog("Error canceling thread_dac");
		}
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
//  offen lassen !!! sonst spint lirc bei system()
//	close(STDIN_FILENO);
//	close(STDOUT_FILENO);
//	close(STDERR_FILENO);
}

int main(int argc, char **argv) {
	// daemonize();

	flog = fopen(LOGFILE, "a");
	if (flog == 0) {
		perror("error opening logfile " LOGFILE);
		exit(EXIT_FAILURE);
	}

	mcplog("MCP initializing");

	/* install signal handler */
	if (signal(SIGHUP, sig_handler) == SIG_ERR) {
		mcplog("can't catch SIGHUP");
		exit(EXIT_FAILURE);
	}
	if (signal(SIGTERM, sig_handler) == SIG_ERR) {
		mcplog("can't catch SIGTERM");
		exit(EXIT_FAILURE);
	}
	if (signal(SIGINT, sig_handler) == SIG_ERR) {
		mcplog("can't catch SIGINT");
		exit(EXIT_FAILURE);
	}

	/* setup wiringPi */
#ifdef WIRINGPI
	if (wiringPiSetup() == -1) {
		mcplog("Unable to start wiringPi");
		return 2;
	}
#endif

	/* initialize modules */
	mpdclient_init();
	power_init();
	dac_init();

#if defined(LIRC_RECEIVE) || defined(LIRC_SEND)
	lirc_init();
#endif

#ifdef DEVINPUT
	devinput_init();
#endif

	/* create thread for each module */

#ifdef LIRC_RECEIVE
	if (pthread_create(&thread_lirc, NULL, &lirc, NULL)) {
		mcplog("Error creating thread_lirc");
	}
#endif

#ifdef DEVINPUT
	if (pthread_create(&thread_devinput, NULL, &devinput, NULL)) {
		mcplog("Error creating thread_devinput");
	}
#endif

	if (pthread_create(&thread_mpdclient, NULL, &mpdclient, NULL)) {
		mcplog("Error creating thread_mpdclient");
	}

	if (pthread_create(&thread_dac, NULL, &dac, NULL)) {
		mcplog("Error creating thread_dac");
	}

	mcplog("MCP online");

	/* wait for threads to finish */

	if (pthread_join(thread_dac, NULL)) {
		mcplog("Error joining thread_dac");
	}

	if (pthread_join(thread_mpdclient, NULL)) {
		mcplog("Error joining thread_mpdclient");
	}

#ifdef LIRC_RECEIVE
	if (pthread_join(thread_lirc, NULL)) {
		mcplog("Error joining thread_lirc");
	}
#endif

#ifdef DEVINPUT
	if (pthread_join(thread_devinput, NULL)) {
		mcplog("Error joining thread_devinput");
	}
#endif

	/* shutdown modules */
	dac_close();

	mcplog("MCP terminated");
	fclose(flog);
	return 0;
}
