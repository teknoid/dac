#include "display-menu.h"

#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "mcp.h"
#include "utils.h"

static menu_t *menu = NULL;

static void menu_down() {
	if (menu) {
		menu_driver(menu->cmenu, REQ_DOWN_ITEM);
		wrefresh(menu->cwindow);
	}
}

static void menu_up() {
	if (menu) {
		menu_driver(menu->cmenu, REQ_UP_ITEM);
		wrefresh(menu->cwindow);
	}
}

static void menu_select() {
	if (menu) {
		ITEM *citem = current_item(menu->cmenu);
		menuitem_t *item = item_userptr(citem);

		// open back menu
		if (!item) {
			if (!menu->back) {
				mcp->menu = 0;
				xlog("leaving menu mode");
			} else {
				menu_open(menu->back);
			}
			return;
		}

		// open sub menu
		if (item->submenu) {
			menu_open(item->submenu);
			return;
		}

		// execute menu function with configuration and selected item value
		if (menu->config) {
			const menuconfig_t *config = menu->config;
			(config->setfunc)(config, item->value);
			return;
		}

		// execute void item function
		if (item->vfunc) {
			mcp->menu = 0;
			xlog("executing void function for %s", item->name);
			(*item->vfunc)();
			return;
		}

		// execute integer item function
		if (item->ifunc) {
			mcp->menu = 0;
			xlog("executing integer function for %s with %d", item->name, item->value);
			(*item->ifunc)(item->value);
			return;
		}
	}
}

void menu_create(menu_t *menu, menu_t *parent) {
	int length = menu->size;
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

void menu_open(menu_t *m) {
	menu = m;
	xlog("painting '%s'", menu->title);
	if (menu->config) {
		const menuconfig_t *config = menu->config;
		int current = (config->getfunc)(config);
		// TODO mark current selected value
	}
	redrawwin(menu->cwindow);
	wrefresh(menu->cwindow);
}

// !!! DO NOT use key names from linux/input.h - this breaks curses.h !!!
void menu_handle(int c) {
	switch (c) {
	case 0x42:	// down
	case 115:	// KEY_VOLUMEUP
	case KEY_DOWN:
		menu_down();
		break;
	case 0x41:	// up
	case 114:	// KEY_VOLUMEDOWN
	case KEY_UP:
		menu_up();
		break;
	case 207:	// KEY_PLAY
	case 99:	// KEY_SYSRQ
	case 0x0d:
	case '\n':
		menu_select();
		break;
	case 59:	// KEY_F1
	case 128:	// KEY_STOP
	case 'q':
		mcp->menu = 0;
		xlog("leaving menu mode");
		break;
	}
}

void menu_show_selection(int value) {
	xlog("show_selection");
}
