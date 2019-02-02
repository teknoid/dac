#include <curses.h>
#include <menu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "display-menu-options.h"
#include "utils.h"

#define MAX(a, b) ((a) > (b)) ? (a) : (b)

static char *selected_item;
static MENU *menu = NULL;
static WINDOW *menu_window = NULL;

/*
 * Create a menu with items.
 */
static ITEM **create_menu_items(menu_t *m) {
	const menuoption_t *items = m->items;
	int length = m->items_size;
	xlog("creating %s with %d entries", m->title, length);

	ITEM **mitems = malloc((length + 2) * sizeof(mitems[0])); // +back +NULL
	if (mitems == NULL) {
		xlog("not enough memory");
		exit(EXIT_FAILURE);
	}
	for (int i = 0; i < length; ++i) {
		/* make menu item */
		mitems[i] = new_item(items[i].name, items[i].descr);
		/* set item_userptr to point to function to execute for this option */
		set_item_userptr(mitems[i], (void*) &items[i]);
	}

	/* back item (no item_userptr) */
	if (m == &m0) {
		mitems[length] = new_item("Exit", "Exit");
	} else {
		mitems[length] = new_item("Back", "Back");
	}
	set_item_userptr(mitems[length], NULL);

	/* NULL */
	mitems[length + 1] = NULL;
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

static void menu_show(menu_t *m) {
	// close if open
	menu_close();

	// exit?
	if (!m) {
		mcp->menu = 0;
		refresh();
		return;
	}

	// create new menu
	ITEM **mitems = create_menu_items(m);
	menu = new_menu(mitems);
	menu->userptr = m;
	menu_window = create_menu_window();
	post_menu(menu);
	wrefresh(menu_window);
}

void menu_open() {
	mcp->menu = 1;
	menu_back_connect();
	menu_show(&m0);
}

void menu_close() {
	if (menu) {
		unpost_menu(menu);
		wrefresh(menu_window);
		ITEM **mitems = menu_items(menu);
		for (int i = 0; i < ARRAY_SIZE(mitems); ++i) {
			free_item(mitems[i]);
		}
		free_menu(menu);
		delwin(menu_window);
		menu = NULL;
	}
}

void menu_select() {
	ITEM *cur = current_item(menu);
	selected_item = (char *) item_name(cur);
	menuoption_t *current = item_userptr(cur);

	// open back menu
	if (!current) {
		menu_t *m = menu->userptr;
		menu_show(m->back);
		return;
	}

	// open sub menu
	if (current->submenu) {
		xlog("sub menu");
		menu_show(current->submenu);
		return;
	}

	// execute a function
	if (current->fptr) {
		xlog("function");
		(*current->fptr)();
	}
}

void menu_down() {
	if (menu) {
		menu_driver(menu, REQ_DOWN_ITEM);
		wrefresh(menu_window);
	}
}

void menu_up() {
	if (menu) {
		menu_driver(menu, REQ_UP_ITEM);
		wrefresh(menu_window);
	}
}

void show_selection() {
	xlog(selected_item);
}
