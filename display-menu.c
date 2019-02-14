#include "display-menu.h"

#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "mcp.h"
#include "utils.h"

static menu_t *menu = NULL;
static int current;

static void menu_get_selected() {
	if (!menu)
		return;

	if (menu->config) {
		const menuconfig_t *config = menu->config;
		current = (config->getfunc)(config);
		xlog("register %02d, mask 0b%s, value %d", config->reg, printBits(config->mask), current);

		if (!menu->cmenu) {
			// direct input menu without items -> print current value
			mvwprintw(menu->cwindow, 3, (WIDTH / 2) - 1, "%02d", current);
		} else {
			// we have items -> mark current as not selectable
			for (ITEM **citem = menu->cmenu->items; *citem; citem++) {
				menuitem_t *item = item_userptr(*citem);
				if (item && item->value == current) {
					item_opts_off(*citem, O_SELECTABLE);
				} else {
					item_opts_on(*citem, O_SELECTABLE);
				}
			}
		}
	}

	redrawwin(menu->cwindow);
	wrefresh(menu->cwindow);
}

static void menu_set_selected(int value) {
	if (!menu)
		return;

	if (!menu->config)
		return;

	const menuconfig_t *config = menu->config;
	(config->setfunc)(config, value);
}

static void menu_down() {
	if (!menu)
		return;

	// we have items, let menu driver do it's work
	if (menu->cmenu) {
		menu_driver(menu->cmenu, REQ_DOWN_ITEM);
		wrefresh(menu->cwindow);
		return;
	}

	// check and store decremented value
	if (menu->config && current > menu->config->min) {
		menu_set_selected(--current);
		menu_get_selected();
	}
}

static void menu_up() {
	if (!menu)
		return;

	// we have items, let menu driver do it's work
	if (menu->cmenu) {
		menu_driver(menu->cmenu, REQ_UP_ITEM);
		wrefresh(menu->cwindow);
		return;
	}

	// check and store incremented value
	if (menu->config && current < menu->config->max) {
		menu_set_selected(++current);
		menu_get_selected();
	}
}

static void menu_select() {
	if (!menu)
		return;

	// direct input menu without items - go back
	if (!menu->cmenu) {
		menu_open(menu->back);
		return;
	}

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

	// write selected value with config's setter function and read back
	menu_set_selected(item->value);
	menu_get_selected();
}

void menu_create(menu_t *menu, menu_t *parent) {
	menu->back = parent;

	// create a window for the menu
	WINDOW *cwindow = newwin(HEIGHT, WIDTH, 0, 0);
	menu->cwindow = cwindow;
	wbkgd(cwindow, COLOR_PAIR(YELLOWONBLUE) | A_BOLD);
	wborder(cwindow, 0, 0, 0, 0, 0, 0, 0, 0);
	// set window title
	int center_pos = (int) (WIDTH / 2) - (strlen(menu->title) / 2);
	mvwprintw(cwindow, 0, center_pos - 2, " %s ", menu->title);

	if (!menu->items) {
		// TODO windows too small & overwriting borders
		// mvwprintw(cwindow, 3, 1, menu->descr);
		return;
	}

	// we have items then build the curses menu
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
	set_menu_format(cmenu, HEIGHT - 2, 1); // 5 rows, 1 column
	set_menu_mark(cmenu, NULL);
	set_menu_fore(cmenu, COLOR_PAIR(REDONWHITE) | A_BOLD | A_REVERSE);
	set_menu_back(cmenu, COLOR_PAIR(YELLOWONBLUE) | A_BOLD);
	set_menu_grey(cmenu, COLOR_PAIR(CYANONBLUE));
	set_menu_mark(cmenu, "*");

	// use full width height (without box and one space left/right)
	set_menu_win(cmenu, cwindow);
	set_menu_sub(cmenu, derwin(cwindow, HEIGHT - 2, WIDTH - 4, 1, 2));
	post_menu(cmenu);
}

void menu_open(menu_t *m) {
	menu = m;
	xlog("painting '%s'", menu->title);
	// get current value from config's getter function
	menu_get_selected();
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
