#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <memory.h>
#include <pthread.h>
#include <ncurses.h>

#include <mpd/status.h>

#include "mcp.h"

#define WIDTH			20
#define HEIGHT			7

#define HEADER			0
#define MAINAREA		2
#define FOOTER			6

#define SCROLLDELAY		4

// #define LOCALMAIN

#define	WHITE			1
#define RED				2
#define YELLOW			3
#define GREEN			4

int scroller_artist = 0;
int scroller_title = 0;

static void check_nightmode() {
	if (mcp->nightmode) {
		attroff(A_BOLD);
	} else {
		attron(A_BOLD);

	}
}

static void center_line(int line, char *text) {
	int pos = 10 - (strlen(text) / 2);
	mvprintw(line, pos, "%s", text);
}

static void scroll_line(int line, int *scrollptr, char *text) {
	char scrolltxt[WIDTH + 1];
	unsigned int l = strlen(text);
	unsigned int pos;
	int x = *scrollptr - SCROLLDELAY;
	if (x < 0) {
		pos = 0; // wait 5s before start scrolling
	} else if (x > l - WIDTH) {
		pos = l - WIDTH; // wait 5s before reset
		if (x > l - WIDTH + SCROLLDELAY) {
			*scrollptr = 0; // reset
			pos = 0;
		}
	} else {
		pos = x;
	}
	strncpy(scrolltxt, text + pos, WIDTH);
	scrolltxt[WIDTH] = 0x00;
	// mcplog("%d : %d : %s", *scroller, pos, scrolltxt);
	mvprintw(line, 0, "%s", scrolltxt);
	*scrollptr = *scrollptr + 1;
}

static void audioinfo(int line) {
	mvprintw(line, 0, "%2ddB", mcp->dac_volume);
	if (mcp->dac_signal == nlock) {
		mvaddstr(line, 8, "NLOCK");
		mvaddstr(line, 15, "--/--");
	} else if (mcp->dac_signal == pcm) {
		mvaddstr(line, 9, "PCM");
		mvprintw(line, mcp->dac_rate > 100 ? 14 : 15, "%d/%d", mcp->mpd_bits, mcp->dac_rate);
	} else if (mcp->dac_signal == dsd) {
		if (mcp->dac_rate == 44) {
			mvaddstr(line, 9, "DSD64");
		} else if (mcp->dac_rate == 88) {
			mvaddstr(line, 8, "DSD128");
		} else if (mcp->dac_rate == 176) {
			mvaddstr(line, 8, "DSD256");
		} else if (mcp->dac_rate == 384) {
			mvaddstr(line, 8, "DSD512");
		} else if (mcp->dac_rate == 176) {
			mvaddstr(line, 9, "DSD?");
		}
		mvprintw(line, 18, "%d", mcp->dac_rate);
	} else if (mcp->dac_signal == dop) {
		mvaddstr(line, 9, "DOP");
		mvprintw(line, mcp->dac_rate > 100 ? 14 : 15, "%d/%d", mcp->dac_bits, mcp->dac_rate);
	}
}

static void songinfo(int line) {
	mvprintw(line, mcp->plist_pos > 100 ? 6 : 7, "[%d:%d]", mcp->plist_key, mcp->plist_pos);
	if (strlen(mcp->artist) <= WIDTH) {
		center_line(line + 1, mcp->artist);
	} else {
		scroll_line(line + 1, &scroller_artist, mcp->artist);
	}
	if (strlen(mcp->title) <= WIDTH) {
		center_line(line + 2, mcp->title);
	} else {
		scroll_line(line + 2, &scroller_title, mcp->title);
	}
}

static void systeminfo(int line) {
	if (mcp->clock_tick) {
		mvprintw(line, 0, "%d:%02d", mcp->clock_h, mcp->clock_m);
	} else {
		mvprintw(line, 0, "%d %02d", mcp->clock_h, mcp->clock_m);
	}
	if (mcp->temp >= 60) {
		color_set(RED, NULL);
		mvprintw(line, 8, "%2.1f", mcp->temp);
	} else if (mcp->temp >= 50) {
		attron(A_BOLD);
		color_set(YELLOW, NULL);
		mvprintw(line, 8, "%2.1f", mcp->temp);
	} else {
		color_set(GREEN, NULL);
		mvprintw(line, 8, "%2.1f", mcp->temp);
	}
	color_set(WHITE, NULL);
	mvprintw(line, 16, "%1.2f", mcp->load);
}

static void paint_play() {
	clear();
	curs_set(0);
	color_set(WHITE, NULL);
	check_nightmode();
	audioinfo(HEADER);
	attron(A_BOLD);
	songinfo(MAINAREA);
	check_nightmode();
	systeminfo(FOOTER);
	refresh();
}

static void paint_stop() {
	clear();
	curs_set(0);
	color_set(WHITE, NULL);
	check_nightmode();
	audioinfo(HEADER);
	attroff(A_BOLD);
	songinfo(MAINAREA);
	check_nightmode();
	systeminfo(FOOTER);
	refresh();
}

static void paint_stdby() {
	// motd / uname / df -h
	clear();
	curs_set(0);
	color_set(WHITE, NULL);
	check_nightmode();
	systeminfo(3);
	refresh();
}

static void paint_off() {
	clear();
	color_set(WHITE, NULL);
	attron(A_BOLD);
	mvaddstr(3, 2, "system shutdown");
	refresh();
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

void display_update() {
	if (mcp->power == off) {
		paint_off();
		return;
	}
	if (mcp->power == stdby || mcp->power == startup) {
		paint_stdby();
		return;
	}
	if (mcp->mpd_state == MPD_STATE_STOP || mcp->mpd_state == MPD_STATE_PAUSE) {
		paint_stop();
		return;
	}
	paint_play();
}

int display_init() {
	FILE *i, *o;
	i = fopen(DISPLAY, "r");
	o = fopen(DISPLAY, "w");

	newterm(0, o, i);

	clear();
	refresh();

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
		switch (count) {
		case 0:
			get_system_status();
			mcp->clock_tick = 0;
			break;
		case 2:
			mcp->clock_tick = 1;
			break;
		}

		if (mcp->power == off) {
			msleep(500);
			count += 2;
		} else {
			msleep(250);
			count += 1;
		}

		if (count >= 4) {
			count = 0;
		}

		display_update();
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