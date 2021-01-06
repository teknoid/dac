#include "display.h"

#include <curses.h>
#include <mpd/status.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "display-sysfont.h"
#include "display-menu.h"
#include "utils.h"

#define msleep(x) usleep(x*1000)

// #define LOCALMAIN

#ifdef LOCALMAIN
#include "dac-es9028.h"
#undef DISPLAY
#define DISPLAY			"/dev/tty"
#else
#include "mcp.h"
#endif

static char fullscreen[4]; // xxx\0
static int countdown_fullscreen;
static int countdown_menu;
static int scroller_artist = 0;
static int scroller_title = 0;

static pthread_t thread_display;
static void *display(void *);

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
	// xlog("%d : %d : %s", *scroller, pos, scrolltxt);
	mvprintw(line, 0, "%s", scrolltxt);
	*scrollptr = *scrollptr + 1;
}

static void dotchar(unsigned int startx, unsigned int starty, char c) {
	int i = (c - 0x20) * 6;
	for (int x = 0; x < 6; x++) {
		unsigned char bit = _font_bits[i + x];
		for (int y = 0; y < 7; y++) {
			if (bit & 0b00000001) {
				mvaddch(starty + y, startx + x, FULLSCREEN_CHAR);
			}
			bit >>= 1;
		}
	}
}

static void audioinfo(int line) {
	if (mcp->dac_mute) {
		mvprintw(line, 0, "---");
	} else {
		mvprintw(line, 0, "%2ddB", mcp->dac_volume);
	}
	if (mcp->dac_signal == nlock) {
		mvaddstr(line, 8, "NLOCK");
		mvaddstr(line, 15, "--/--");
		return;
	}
	if (mcp->dac_source == opt) {
		mvaddstr(line, 9, "OPT");
		mvprintw(line, mcp->dac_rate > 100 ? 14 : 15, "%dkHz", mcp->dac_rate);
		return;
	}
	if (mcp->dac_source == coax) {
		mvaddstr(line, 8, "COAX");
		mvprintw(line, mcp->dac_rate > 100 ? 14 : 15, "%dkHz", mcp->dac_rate);
		return;
	}
	if (mcp->dac_signal == pcm) {
		mvaddstr(line, strlen(mcp->extension) == 4 ? 8 : 9, mcp->extension);
		mvprintw(line, mcp->dac_rate > 100 ? 14 : 15, "%d/%d", mcp->mpd_bits, mcp->dac_rate);
		return;
	}
	if (mcp->dac_signal == dsd) {
		if (mcp->dac_rate == 44) {
			mvaddstr(line, 9, "DSD64");
		} else if (mcp->dac_rate == 88) {
			mvaddstr(line, 8, "DSD128");
		} else if (mcp->dac_rate == 176) {
			mvaddstr(line, 8, "DSD256");
		} else if (mcp->dac_rate == 384) {
			mvaddstr(line, 8, "DSD512");
		} else if (mcp->dac_rate == 768) {
			mvaddstr(line, 7, "DSD1024");
		} else {
			mvaddstr(line, 9, "DSD?");
		}
		mvprintw(line, mcp->dac_rate > 100 ? 17 : 18, "%d", mcp->dac_rate);
		return;
	}
	if (mcp->dac_signal == dop) {
		mvaddstr(line, 9, "DOP");
		mvprintw(line, mcp->dac_rate > 100 ? 17 : 18, "%d", mcp->dac_rate);
	}
	mvaddstr(line, 9, "???");
	mvprintw(line, 18, "%d", mcp->dac_rate);
}

static void songinfo(int line) {
	int x;
	if (mcp->plist_pos < 10) {
		x = 8;
	} else if (mcp->plist_pos > 100) {
		x = 7;
	} else {
		x = 7;
	}
	mvprintw(line, x, "[%d:%d]", mcp->plist_key, mcp->plist_pos);
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
	mvprintw(line, 0, "%d:%02d", mcp->clock_h, mcp->clock_m);
	if (mcp->temp >= 60) {
		color_set(RED, NULL);
		mvprintw(line, 8, "%2.1f", mcp->temp);
	} else if (mcp->temp >= 50) {
		attron(A_BOLD);
		color_set(YELLOW, NULL);
		mvprintw(line, 8, "%2.1f", mcp->temp);
		check_nightmode();
	} else {
		color_set(GREEN, NULL);
		mvprintw(line, 8, "%2.1f", mcp->temp);
	}
	color_set(WHITE, NULL);
	mvprintw(line, 16, "%1.2f", mcp->load);
}

static void paint_play() {
	curs_set(0);
	color_set(WHITE, NULL);
	check_nightmode();
	audioinfo(HEADER);
	attron(A_BOLD);
	songinfo(MAINAREA);
	check_nightmode();
	systeminfo(FOOTER);
}

static void paint_stop() {
	curs_set(0);
	color_set(WHITE, NULL);
	check_nightmode();
	audioinfo(HEADER);
	attroff(A_BOLD);
	songinfo(MAINAREA);
	check_nightmode();
	systeminfo(FOOTER);
}

static void paint_stdby() {
	// TODO motd / uname / df -h
	curs_set(0);
	color_set(WHITE, NULL);
	check_nightmode();
	systeminfo(CENTER);
}

static void paint_source_ext() {
	curs_set(0);
	color_set(WHITE, NULL);
	check_nightmode();
	audioinfo(HEADER);
	attroff(A_BOLD);
	systeminfo(FOOTER);
}

static void paint_fullscreen() {
	color_set(WHITE, NULL);
	attron(A_BOLD);
	unsigned int x = 0;
	for (int i = 0; i < strlen(fullscreen); i++) {
		char c = fullscreen[i];
		dotchar(x, 0, c);
		x += 6;
	}
}

static void paint() {
	if (--countdown_fullscreen > 0) {
		return; // still in fullscreen mode
	}
	if (mcp->menu) {
		if (--countdown_menu == 0) {
			mcp->menu = 0; // no input -> close
			xlog("menu timeout");
		} else {
			return; // still in menu mode
		}
	} else {
		countdown_menu = 0;
	}

	clear();
	if (!mcp->dac_power) {
		paint_stdby();
	} else if (mcp->dac_source != mpd) {
		paint_source_ext();
	} else if (mcp->mpd_state == MPD_STATE_PLAY) {
		paint_play();
	} else {
		paint_stop();
	}
	refresh();
}

static void clear_clocktick() {
	if (countdown_fullscreen || mcp->menu) {
		return;
	}
	if (!mcp->dac_power) {
		mvaddch(CENTER, mcp->clock_h < 10 ? 1 : 2, ' ');
	} else {
		mvaddch(FOOTER, mcp->clock_h < 10 ? 1 : 2, ' ');
	}
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

void display_fullscreen_number(int value) {
	clear();
	countdown_fullscreen = 10;
	sprintf(fullscreen, "%d", value);
	paint_fullscreen();
	refresh();
}

void display_fullscreen_string(char *value) {
	clear();
	countdown_fullscreen = 10;
	sprintf(fullscreen, "%s", value);
	paint_fullscreen();
	refresh();
}

int display_init() {
	FILE *i, *o;
	i = fopen(DISPLAY, "r");
	o = fopen(DISPLAY, "w");

	SCREEN *screen = newterm("linux", o, i);
	set_term(screen);

	clear();
	refresh();

	if (has_colors() == FALSE) {
		endwin();
		xlog("Your terminal does not support colors");
		return -1;
	}

	start_color();
	init_pair(WHITE, COLOR_WHITE, COLOR_BLACK);
	init_pair(RED, COLOR_RED, COLOR_BLACK);
	init_pair(YELLOW, COLOR_YELLOW, COLOR_BLACK);
	init_pair(GREEN, COLOR_GREEN, COLOR_BLACK);
	init_pair(YELLOWONBLUE, COLOR_YELLOW, COLOR_BLUE);
	init_pair(REDONWHITE, COLOR_RED, COLOR_WHITE);
	init_pair(CYANONBLUE, COLOR_CYAN, COLOR_BLUE);

	cbreak();
	noecho();
	curs_set(0); /* set cursor to be invisible */
	keypad(stdscr, TRUE); /* enable keypad */

	// start painter thread
	if (pthread_create(&thread_display, NULL, &display, NULL)) {
		xlog("Error creating thread_display");
		return -1;
	}

	xlog("DISPLAY initialized");
	return 0;
}

void display_close() {
	if (pthread_cancel(thread_display)) {
		xlog("Error canceling thread_display");
	}
	if (pthread_join(thread_display, NULL)) {
		xlog("Error joining thread_display");
	}
	endwin();
}

void display_menu_mode() {
	xlog("menu mode");
	mcp->menu = 1;
	countdown_menu = 30;
}

void *display(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	while (1) {
		get_system_status();
		paint();

		if (!mcp->dac_power) {
//			msleep(500);
//			clear_clocktick();
			msleep(500 * 10);
		} else {
			msleep(250);
			paint();
			msleep(250);
			paint();
//			clear_clocktick();
			msleep(250);
			paint();
			msleep(250);
		}
	}
}

#ifdef LOCALMAIN

mcp_state_t *mcp;
mcp_config_t *cfg;

void mpdclient_handle(int key) {
}
void mcp_system_shutdown() {
}
void mcp_system_reboot() {
}
int mcp_status_get(const void *p1, const void *p2) {
	return 0;
}
void mcp_status_set(const void *p1, const void *p2, int value) {
}

int main(void) {
	cfg = malloc(sizeof(*cfg));
	mcp = malloc(sizeof(*mcp));
	strcpy(mcp->artist, "The KLF");
	strcpy(mcp->title, "Justified & Ancient (Stand by the Jams)");
	strcpy(mcp->album, "");
	mcp->dac_power = 1;
	mcp->mpd_state = MPD_STATE_PLAY;
	display_init();
	es9028_prepare_menus();

	int z = -23;
	while (1) {
		int c = getch();

		if (mcp->menu) {
			display_menu_mode();
			menu_handle(c);
			continue;
		}

		if (c == 'q') {
			break;
		}

		switch (c) {
			case KEY_DOWN:
			display_fullscreen_number(--z);
			break;
			case KEY_UP:
			display_fullscreen_number(++z);
			break;
			case '\n':
			display_menu_mode();
			menu_open(&m_main);
			break;
		}
	}

	display_close();
	return EXIT_SUCCESS;
}
#endif
