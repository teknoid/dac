#include <curses.h>
#include <menu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "display-menu-options.h"
#include "utils.h"

#define MAX(a, b) ((a) > (b)) ? (a) : (b)

static MENU *menu = NULL;
static WINDOW *menu_window = NULL;
static menuitem_t *selected;

/*
 * Create a menu with items.
 */
static ITEM **create_menu_items(menu_t *m) {
	const menuitem_t *items = m->items;
	int length = m->items_size;
	xlog("creating %s with %d entries", m->title, length);

	ITEM **mitems = malloc((length + 2) * sizeof(mitems[0])); // +back +NULL
	if (mitems == NULL) {
		xlog("not enough memory");
		exit(EXIT_FAILURE);
	}

	/* make menu items */
	for (int i = 0; i < length; ++i) {
		mitems[i] = new_item(items[i].name, NULL);
		/* set item_userptr to item definition */
		set_item_userptr(mitems[i], (void*) &items[i]);
	}

	/* back item (no item_userptr) */
	if (m == &m_main) {
		mitems[length] = new_item("Exit", NULL);
	} else {
		mitems[length] = new_item("Back", NULL);
	}
	set_item_userptr(mitems[length], NULL);

	// terminate list
	mitems[length + 1] = NULL;
	return mitems;
}

/*
 * Create window for menu.
 */
static WINDOW *create_menu_window(menu_t *m) {
	WINDOW *menu_window = newwin(HEIGHT, WIDTH, 0, 0);
	wbkgd(menu_window, COLOR_PAIR(WHITEONBLUE));
	set_menu_win(menu, menu_window);
	set_menu_sub(menu, derwin(menu_window, HEIGHT - 2, WIDTH - 2, 1, 1));
	set_menu_format(menu, HEIGHT - 2, 1);
	set_menu_mark(menu, " * ");
	set_menu_back(menu, COLOR_PAIR(WHITEONBLUE));
	set_menu_fore(menu, COLOR_PAIR(BLUEONWHITE) | WA_BOLD);
	box(menu_window, 0, 0);
	int center_pos = (int) (WIDTH / 2) - (strlen(m->title) / 2);
	mvwprintw(menu_window, 0, center_pos, "%s", m->title);
	return menu_window;
}

static void menu_paint(menu_t *m) {
	// close any open menu
	menu_close();
	// create and show menu
	ITEM **mitems = create_menu_items(m);
	menu = new_menu(mitems);
	menu->userptr = m;
	menu_window = create_menu_window(m);
	post_menu(menu);
	wrefresh(menu_window);
	refresh();
}

void menu_open() {
	menu_paint(&m_main);
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
		refresh();
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

void menu_select() {
	if (menu) {
		ITEM *cur = current_item(menu);
		selected = item_userptr(cur);

		// open back menu
		if (!selected) {
			menu_t *m = menu->userptr;
			if (m->back) {
				menu_paint(m->back);
			} else {
				display_menu_exit();
			}
			return;
		}

		// open sub menu
		if (selected->submenu) {
			xlog("sub menu");
			menu_close();
			menu_paint(selected->submenu);
			return;
		}

		// execute item function
		if (selected->func) {
			display_menu_exit();
			xlog("executing void function");
			(*selected->func)();
			return;
		}
		if (selected->ifunc) {
			display_menu_exit();
			xlog("executing int function with %d", selected->ifunc_arg);
			(*selected->ifunc)(selected->ifunc_arg);
			return;
		}
	}
}

void show_selection(int value) {
	xlog("name %s value %d", selected->name, value);
}
