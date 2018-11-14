#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <memory.h>
#include <pthread.h>
#include <ncurses.h>

#include <mpd/status.h>

#include "mcp.h"

#define TTY				"/dev/tty0"

#define HEADER			0
#define FOOTER			6

// #define LOCALMAIN

screen_t *screen;

static void screen_paint(int count) {
	clear();
	color_set(1, NULL);
	attron(A_BOLD);

	// header line
	color_set(1, NULL);
	mvprintw(HEADER, 0, "%2ddB", screen->dac_volume);
	if (screen->dac_signal == NLOCK) {
		mvaddstr(HEADER, 8, "NLOCK");
		mvaddstr(HEADER, 15, "--/--");
	} else if (screen->dac_signal == PCM) {
		mvaddstr(HEADER, 9, "PCM");
		mvprintw(HEADER, 15, "%d/%d", screen->mpd_bits, screen->dac_rate);
	} else if (screen->dac_signal == DSD) {
		mvaddstr(HEADER, 9, "DSD");
		mvprintw(HEADER, 15, "%d/%d", screen->dac_bits, screen->dac_rate);
	} else if (screen->dac_signal == DOP) {
		mvaddstr(HEADER, 9, "DOP");
		mvprintw(HEADER, 15, "%d/%d", screen->dac_bits, screen->dac_rate);
	}

	// main area
	if (screen->mpd_state == MPD_STATE_PAUSE || screen->mpd_state == MPD_STATE_STOP) {
		attroff(A_BOLD);
	}
	mvprintw(2, 0, "%s", screen->artist);
	mvprintw(3, 0, "%s", screen->title);
	// mvprintw(4, 0, "%s", screen->album);
	attron(A_BOLD);

	// footer line
	if (count % 2 == 0) {
		mvprintw(FOOTER, 0, "%d %02d", screen->clock_h, screen->clock_m);
	} else {
		mvprintw(FOOTER, 0, "%d:%02d", screen->clock_h, screen->clock_m);
	}
	if (screen->temp >= 50) {
		color_set(2, NULL);
		mvprintw(FOOTER, 8, "%2.1f", screen->temp);
		color_set(1, NULL);
	} else {
		color_set(3, NULL);
		mvprintw(FOOTER, 8, "%2.1f", screen->temp);
		color_set(1, NULL);
	}
	mvprintw(FOOTER, 16, "%1.2f", screen->load);

	refresh();
}

static void screen_update_system() {
	time_t timer;
	time(&timer);

	struct tm* tm_info = localtime(&timer);
	screen->clock_h = tm_info->tm_hour;
	screen->clock_m = tm_info->tm_min;

	double load[3];
	if (getloadavg(load, 3) != -1) {
		screen->load = load[0];
	}

	unsigned long temp = 0;
	FILE *fp = fopen("/sys/devices/virtual/thermal/thermal_zone0/temp", "r");
	if (fp) {
		if (fscanf(fp, "%lu", &temp) == 1) {
			screen->temp = temp / 1000.0;
		}
		fclose(fp);
	}
}

screen_t *display_get_screen() {
	return screen;
}

int display_init() {
	screen = malloc(sizeof(*screen));

	screen->artist = malloc(BUFSIZE);
	screen->title = malloc(BUFSIZE);
	screen->album = malloc(BUFSIZE);

	memset(screen->artist, 0, BUFSIZE);
	memset(screen->title, 0, BUFSIZE);
	memset(screen->album, 0, BUFSIZE);

	FILE *i, *o;
	i = fopen(TTY, "r");
	o = fopen(TTY, "w");

	newterm(0, o, i);
	curs_set(0);
	start_color();

	if (has_colors() && COLOR_PAIRS >= 13) {
		init_pair(1, COLOR_WHITE, COLOR_BLACK);
		init_pair(2, COLOR_RED, COLOR_BLACK);
		init_pair(3, COLOR_GREEN, COLOR_BLACK);
		init_pair(4, COLOR_YELLOW, COLOR_BLACK);
		init_pair(5, COLOR_BLUE, COLOR_BLACK);
		init_pair(6, COLOR_MAGENTA, COLOR_BLACK);
		init_pair(6, COLOR_CYAN, COLOR_BLACK);
		init_pair(8, COLOR_BLUE, COLOR_WHITE);
		init_pair(9, COLOR_WHITE, COLOR_RED);
		init_pair(10, COLOR_BLACK, COLOR_GREEN);
		init_pair(11, COLOR_BLUE, COLOR_YELLOW);
		init_pair(12, COLOR_WHITE, COLOR_BLUE);
		init_pair(13, COLOR_WHITE, COLOR_MAGENTA);
	}
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
			screen_update_system();
		}
		screen_paint(count);
		usleep(500 * 1000);
		count++;
	}
}

#ifdef LOCALMAIN
int main(void) {
	display_init();

	char *artist = "Mike Koglin";
	char *title = "The Silence (Prospekt Remix)";
	char *album = "Ministry Of Sound";

	screen->state = MPD_STATE_PAUSE;
	screen->volume = -42;
	screen->signal = 2;
	screen->bits = 24;
	screen->rate = 96;
	screen->temp = 51.2;
	screen->load = 0.23;
	screen->artist = artist;
	screen->title = title;
	screen->album = album;

	display(NULL);

	display_close();
	return EXIT_SUCCESS;
}
#endif
