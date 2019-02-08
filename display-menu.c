#include <curses.h>
#include <menu.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "display-menu-options.h"
#include "utils.h"

static menu_t *menu = NULL;

static void create_menu(menu_t *menu, menu_t *parent) {
	int length = menu->items_size;
	xlog("creating '%s' with %d entries", menu->title, length);

	ITEM **citems = malloc((length + 2) * sizeof(citems[0])); // +back +NULL
	if (citems == NULL) {
		xlog("not enough memory");
		exit(EXIT_FAILURE);
	}

	// make menu items
	const menuitem_t *items = menu->items;
	for (int i = 0; i < length; ++i) {
		citems[i] = new_item(items[i].name, NULL);
		set_item_userptr(citems[i], (void*) &items[i]); // set to menu definition
	}

	// back item with empty item_userptr
	menu->back = parent;
	if (!parent) {
		citems[length] = new_item("Exit", NULL);
	} else {
		citems[length] = new_item("Back", NULL);
	}
	set_item_userptr(citems[length], NULL);

	// NULL terminated list
	citems[length + 1] = NULL;

	// create the menu
	MENU *cmenu = new_menu(citems);
	menu->cmenu = cmenu;
	set_menu_format(cmenu, HEIGHT - 2, 1);
	set_menu_mark(cmenu, " * ");
	set_menu_fore(cmenu, COLOR_PAIR(REDONWHITE) | A_BOLD | A_REVERSE);
	set_menu_back(cmenu, COLOR_PAIR(YELLOWONBLUE) | A_BOLD);

	// create a window for the menu
	WINDOW *cwindow = newwin(HEIGHT, WIDTH, 0, 0);
	menu->cwindow = cwindow;
	wbkgd(cwindow, COLOR_PAIR(YELLOWONBLUE) | A_BOLD);
	wborder(cwindow, 0, 0, 0, 0, 0, 0, 0, 0);
	set_menu_win(cmenu, cwindow);
	set_menu_sub(cmenu, derwin(cwindow, HEIGHT - 2, WIDTH - 2, 1, 1));
	post_menu(cmenu);

	// set window title
	int center_pos = (int) (WIDTH / 2) - (strlen(menu->title) / 2);
	mvwprintw(cwindow, 0, center_pos, "%s", menu->title);
}

void menu_prepare() {
	create_menu(&m_main, NULL);
	create_menu(&m_playlist, &m_main);
	create_menu(&m_input, &m_main);
	create_menu(&m_system, &m_main);
}

void menu_open() {
	if (!menu) {
		menu = &m_main;
	}
	xlog("painting '%s'", menu->title);
	redrawwin(menu->cwindow);
	wrefresh(menu->cwindow);
}

void menu_close() {
	menu = NULL;
}

void menu_down() {
	if (menu) {
		menu_driver(menu->cmenu, REQ_DOWN_ITEM);
		wrefresh(menu->cwindow);
	}
}

void menu_up() {
	if (menu) {
		menu_driver(menu->cmenu, REQ_UP_ITEM);
		wrefresh(menu->cwindow);
	}
}

void menu_select() {
	if (menu) {
		ITEM *citem = current_item(menu->cmenu);
		menuitem_t *item = item_userptr(citem);

		// open back menu
		if (!item) {
			if (!menu->back) {
				display_menu_exit();
			} else {
				menu = menu->back;
				menu_open();
			}
			return;
		}

		// open sub menu
		if (item->submenu) {
			menu = item->submenu;
			menu_open();
			return;
		}

		// execute void item function
		if (item->func) {
			display_menu_exit();
			xlog("executing void function for %s", item->name);
			(*item->func)();
			return;
		}

		// execute integer item function
		if (item->ifunc) {
			display_menu_exit();
			xlog("executing integer function for %s with %d", item->name, item->ifunc_arg);
			(*item->ifunc)(item->ifunc_arg);
			return;
		}
	}
}

void show_selection(int value) {
	xlog("show_selection");
}
