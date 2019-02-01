#include <curses.h>
#include <menu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display-menu-options.h"
#include "mcp.h"
#include "utils.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define MAX(a, b) ((a) > (b)) ? (a) : (b)

static MENU *menu;
static WINDOW *menu_window;

/*
 * Create a menu with items.
 */
static ITEM **create_menu_items(menuoption_t menuoptions[], int n_options) {
	ITEM **mitems = malloc((n_options + 1) * sizeof(mitems[0]));
	if (mitems == NULL) {
		fprintf(stderr, "not enough memory\n");
		exit(EXIT_FAILURE);
	}
	for (int i = 0; i < n_options; ++i) {
		/* make menu item */
		mitems[i] = new_item(menuoptions[i].name, menuoptions[i].descr);
		/* set userptr to point to function to execute for this option */
		set_item_userptr(mitems[i], &menuoptions[i].fptr);
	}
	/* menu library wants null-terminated array */
	mitems[n_options] = NULL;
	return mitems;
}

/*
 * Create window for menu.
 */
static WINDOW *create_menu_window() {
	ITEM **mitems = menu_items(menu);
	/* find maximum size of text for menu items */
	int name_cols = 0;
	/* maximum width of menu names */
	for (int i = 0; i < item_count(menu); ++i) {
		name_cols = MAX(name_cols, strlen(item_name(mitems[i])));
	}
	int descr_cols = 0;
	/* maximum width of menu names */
	for (int i = 0; i < item_count(menu); ++i) {
		descr_cols = MAX(descr_cols, strlen(item_description(mitems[i])));
	}

	/* make menu window */
	char *marker = "*";
	WINDOW * menu_window = newwin(item_count(menu), strlen(marker) + name_cols + 1 + descr_cols, 0, 0);
	set_menu_win(menu, menu_window);
	set_menu_mark(menu, marker);
	box(menu_window, ACS_HLINE, ACS_VLINE);
	return menu_window;
}

void menu_open(void) {
	mcp->menu = 1;

	/* create menu with its own window */
	int n_options = ARRAY_SIZE(menuoptions);
	ITEM **mitems = create_menu_items(menuoptions, n_options);

	menu = new_menu(mitems);
	menu_window = create_menu_window();

	post_menu(menu);
	wrefresh(menu_window);

	menu_driver(menu, REQ_DOWN_ITEM);
	wrefresh(menu_window);
}

void menu_exit(char *x) {
	unpost_menu(menu);
	wrefresh(menu_window);

	ITEM **mitems = menu_items(menu);
	for (int i = 0; i < ARRAY_SIZE(mitems); ++i) {
		free_item(mitems[i]);
	}

	free_menu(menu);
	delwin(menu_window);

	mcp->menu = 0;
}

/*
 * Function to be called for most menu items -- display text in center
 * of screen.
 */
void show_selection(char *selection) {
	xlog(selection);
}

void execute_item() {
	ITEM *cur = current_item(menu);
	func *fptr = item_userptr(cur);
	(*fptr)((char *) item_description(cur));
}

// !!! DO NOT use key names from linux/input.h - this breaks curses.h !!!
void menu_handle(int c) {
	switch (c) {
	case 0x72: // KEY_VOLUMEDOWN
	case KEY_DOWN:
		menu_driver(menu, REQ_DOWN_ITEM);
		wrefresh(menu_window);
		break;
	case 0x73: // KEY_VOLUMEUP
	case KEY_UP:
		menu_driver(menu, REQ_UP_ITEM);
		wrefresh(menu_window);
		break;
	case 0xcf: // KEY_PLAY
	case '\n':
		execute_item();
		break;
	case 0x80: // KEY_STOP
	case 'q':
		menu_exit("xxx");
		break;
	}
}
