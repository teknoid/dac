#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <memory.h>
#include <pthread.h>
#include <ncurses.h>

#include <mpd/status.h>

#include "mcp.h"

#define TTY				"/dev/tty1"
//#define TTY				"/dev/tty"

#define HEADER			0
#define FOOTER			6

// #define LOCALMAIN

#define	WHITE			1
#define RED				2
#define YELLOW			3
#define GREEN			4

static void paint_on() {
	// wie jetzt
}

static void paint_off() {
	// nur status line mittig
}

static void paint_stdby() {
	// motd / uname / df -h
}

static void paint_pause() {
	// wie jetzt mit artist/title 50%
}

static void paint_night() {
	// wie jetzt aber alles 50%
}

static void get_system_status() {
	time_t timer;
	time(&timer);

	struct tm* tm_info = localtime(&timer);
	mcp->clock_h = tm_info->tm_hour;
	mcp->clock_m = tm_info->tm_min;
	if (mcp->clock_h >= 8 && mcp->clock_h < 22) {
		mcp->nightmode = 0;
	} else {
		mcp->nightmode = 1;
	}

	double load[3];
	if (getloadavg(load, 3) != -1) {
		mcp->load = load[0];
	}

	unsigned long temp = 0;
	FILE *fp = fopen("/sys/devices/virtual/thermal/thermal_zone0/temp", "r");
	if (fp) {
		if (fscanf(fp, "%lu", &temp) == 1) {
			mcp->temp = temp / 1000.0;
		}
		fclose(fp);
	}
}

static void check_nightmode() {
	if (mcp->nightmode) {
		attroff(A_BOLD);
	} else {
		attron(A_BOLD);
	}
}

void display_update(int count) {
	clear();
	color_set(1, NULL);
	check_nightmode();

	// header line
	color_set(1, NULL);
	mvprintw(HEADER, 0, "%2ddB", mcp->dac_volume);
	if (mcp->dac_signal == nlock) {
		mvaddstr(HEADER, 8, "NLOCK");
		mvaddstr(HEADER, 15, "--/--");
	} else if (mcp->dac_signal == pcm) {
		mvaddstr(HEADER, 9, "PCM");
		mvprintw(HEADER, mcp->dac_rate > 100 ? 14 : 15, "%d/%d", mcp->mpd_bits, mcp->dac_rate);
	} else if (mcp->dac_signal == dsd) {
		if (mcp->dac_rate == 44) {
			mvaddstr(HEADER, 9, "DSD64");
		} else if (mcp->dac_rate == 88) {
			mvaddstr(HEADER, 8, "DSD128");
		} else if (mcp->dac_rate == 176) {
			mvaddstr(HEADER, 8, "DSD256");
		} else if (mcp->dac_rate == 384) {
			mvaddstr(HEADER, 8, "DSD512");
		} else if (mcp->dac_rate == 176) {
			mvaddstr(HEADER, 9, "DSD?");
		}
		mvprintw(HEADER, 18, "%d", mcp->dac_rate);
	} else if (mcp->dac_signal == dop) {
		mvaddstr(HEADER, 9, "DOP");
		mvprintw(HEADER, mcp->dac_rate > 100 ? 14 : 15, "%d/%d", mcp->dac_bits, mcp->dac_rate);
	}

	// main area
	if (mcp->mpd_state == MPD_STATE_PAUSE || mcp->mpd_state == MPD_STATE_STOP) {
		attroff(A_BOLD);
	} else {
		attron(A_BOLD);
	}
	mvprintw(1, mcp->plist_pos > 100 ? 7 : 8, "[%d:%d]", mcp->plist_key, mcp->plist_pos);
	mvprintw(2, 0, "%s", mcp->artist);
	mvprintw(3, 0, "%s", mcp->title);
	check_nightmode();

	// footer line
	if (count % 2 == 0) {
		mvprintw(FOOTER, 0, "%d %02d", mcp->clock_h, mcp->clock_m);
	} else {
		mvprintw(FOOTER, 0, "%d:%02d", mcp->clock_h, mcp->clock_m);
	}
	if (mcp->temp >= 60) {
		color_set(RED, NULL);
	} else if (mcp->temp >= 50) {
		attron(A_BOLD);
		color_set(YELLOW, NULL);
	} else {
		color_set(GREEN, NULL);
	}
	mvprintw(FOOTER, 8, "%2.1f", mcp->temp);
	check_nightmode();
	color_set(WHITE, NULL);
	mvprintw(FOOTER, 16, "%1.2f", mcp->load);

	refresh();
}

int display_init() {
	FILE *i, *o;
	i = fopen(TTY, "r");
	o = fopen(TTY, "w");

	newterm(0, o, i);
	curs_set(0);

	if (has_colors() == FALSE) {
		endwin();
		mcplog("Your terminal does not support colors");
		return -1;
	}

	start_color();
	init_pair(WHITE, COLOR_WHITE, COLOR_BLACK);
	init_pair(RED, COLOR_RED, COLOR_BLACK);
	init_pair(YELLOW, COLOR_YELLOW, COLOR_BLACK);
	init_pair(GREEN, COLOR_GREEN, COLOR_BLACK);

	clear();
	return 0;
}

void display_close() {
	endwin();
}

void *display(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		perror("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	unsigned char count = 0;
	while (1) {
		if (count % 2 == 0) {
			get_system_status();
		}
		display_update(count);
		msleep(500);
		count++;
	}
}

#ifdef LOCALMAIN
int main(void) {
	display_init();

	char *artist = "Above & Beyond & Gareth Emery Presents Oceanlab";
	char *title = "On A Good Day (Metropolis)";
	char *album = "";

	display(NULL);

	display_close();
	return EXIT_SUCCESS;
}
#endif
