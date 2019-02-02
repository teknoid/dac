#include <curses.h>
#include <menu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "display-menu-options.h"
#include "mcp.h"
#include "utils.h"

#define MAX(a, b) ((a) > (b)) ? (a) : (b)

static char *selected_item;
static ITEM **mitems = NULL;
static MENU *menu = NULL;
static WINDOW *menu_window = NULL;

/*
 * Create a menu with items.
 */
static ITEM **create_menu_items(menuoption_t *options, int size) {
	ITEM **mitems = malloc((size + 1) * sizeof(mitems[0]));
	if (mitems == NULL) {
		fprintf(stderr, "not enough memory\n");
		exit(EXIT_FAILURE);
	}
	for (int i = 0; i < size; ++i) {
		/* make menu item */
		mitems[i] = new_item(options[i].name, options[i].descr);
		/* set userptr to point to function to execute for this option */
		set_item_userptr(mitems[i], &options[i].fptr);
	}
	/* menu library wants null-terminated array */
	mitems[size] = NULL;
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

static void menu_close() {
	unpost_menu(menu);
	wrefresh(menu_window);
	ITEM **mitems = menu_items(menu);
	for (int i = 0; i < ARRAY_SIZE(mitems); ++i) {
		free_item(mitems[i]);
	}
	free_menu(menu);
	delwin(menu_window);
}

static void menu_next(menuoption_t *next, int size) {
	if (menu_window) {
		menu_close();
	}
	if (next == NULL) {
		mcp->menu = 0;
		refresh();
		return;
	}
	mitems = create_menu_items(next, size);
	menu = new_menu(mitems);
	menu_window = create_menu_window();
	post_menu(menu);
	wrefresh(menu_window);
}

void menu_open() {
	mitems = create_menu_items(m0, ARRAY_SIZE(m0));
	menu = new_menu(mitems);
	menu_window = create_menu_window();
	post_menu(menu);
	wrefresh(menu_window);
}

void menu_select() {
	ITEM *cur = current_item(menu);
	func *fptr = item_userptr(cur);
	selected_item = (char *) item_name(cur);
	(*fptr)();
}

void menu_down() {
	if (menu_window) {
		menu_driver(menu, REQ_DOWN_ITEM);
		wrefresh(menu_window);
	}
}

void menu_up() {
	if (menu_window) {
		menu_driver(menu, REQ_UP_ITEM);
		wrefresh(menu_window);
	}
}

/*
 * Function to be called for most menu items -- display text in center
 * of screen.
 */
void show_selection() {
	xlog(selected_item);
}
