#include "display-menu.h"

#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "mcp.h"
#include "utils.h"

#define XPOS_BITS				(WIDTH / 2) - 4
#define YPOS_BITS				3

#define XPOS_VALUE				(WIDTH / 2) - 2
#define YPOS_VALUE				3

static menu_t *menu;
static int current;
static int xpos;

// toggle bit on current position
static void current_bit_toogle() {
	int mask = 1 << xpos;
	if (current & mask) {
		current &= ~mask; // clear
	} else {
		current |= mask; // set
	}
}

static void menu_get_selected() {
	const ITEM *citem = current_item(menu->cmenu);
	const menuitem_t *item = item_userptr(citem);
	const menuconfig_t *config = menu->config;
	current = (config->getfunc)(config, item);
}

static void menu_set_selected() {
	const ITEM *citem = current_item(menu->cmenu);
	const menuitem_t *item = item_userptr(citem);
	const menuconfig_t *config = menu->config;
	(config->setfunc)(config, item, current);
}

static void menu_down() {
	// we have items, let menu driver do it's work
	if (menu->items) {
		menu_driver(menu->cmenu, REQ_DOWN_ITEM);
		return;
	}

	switch (menu->config->style) {

	case bits:
		wchgat(menu->cwindow, 1, A_NORMAL, YELLOWONBLUE, NULL);
		xpos++;
		if (xpos > 8) {
			// position 8 is outside and means "go back"
			xpos = 8;
		}
		return;

	case value:
		// check and store decremented value
		if (current > menu->config->min) {
			--current;
			menu_set_selected();
			menu_get_selected();
		}
		return;

	default:
		return;
	}
}

static void menu_up() {
	// we have items, let menu driver do it's work
	if (menu->items) {
		menu_driver(menu->cmenu, REQ_UP_ITEM);
		return;
	}

	switch (menu->config->style) {

	case bits:
		wchgat(menu->cwindow, 1, A_NORMAL, YELLOWONBLUE, NULL);
		xpos--;
		if (xpos < -1) {
			// position -1 is outside and means "go back"
			xpos = -1;
		}
		return;

	case value:
		// check and store incremented value
		if (current < menu->config->max) {
			++current;
			menu_set_selected();
			menu_get_selected();
		}
		return;

	default:
		return;
	}
}

static void menu_select() {
	ITEM *citem = current_item(menu->cmenu);
	menuitem_t *item = item_userptr(citem);

	// this is the back item -> go to parent menu or exit
	if (!item) {
		if (!menu->back) {
			mcp->menu = 0;
			xlog("leaving menu mode");
			return;
		} else {
			return menu_open(menu->back);
		}
	}

	// item has sub menu -> open sub menu
	if (item->submenu) {
		return menu_open(item->submenu);
	}

	// determine action depending on menuconfig's style
	if (menu->config) {
		switch (menu->config->style) {

		case bits:
			xlog("menu_select bit %i", xpos);
			if (xpos < 0 || xpos > 7) {
				// position outside - go back
				return menu_open(menu->back);
			} else {
				// toggle bit on current position
				current_bit_toogle();
				// write new value with config's setter function and read back
				menu_set_selected();
				menu_get_selected();
			}
			return;

		case value:
			// direct value input by up/down - go back
			xlog("menu_select done, going back");
			return menu_open(menu->back);

		case selection:
			// write selected value with config's setter function and read back
			current = item->index;
			menu_set_selected();
			menu_get_selected();
			return;

		case onoff:
			// toggle value, write with config's setter function and read back
			menu_get_selected();
			if (current) {
				current = 0;
			} else {
				current = 1;
			}
			menu_set_selected();
			return;

		default:
			break;
		}
	}

	// we do not have menuconfig, execute void/integer functions if available
	if (item->vfunc) {
		mcp->menu = 0;
		xlog("executing void function for %s", item->name);
		(*item->vfunc)();
		return;
	}
	if (item->ifunc) {
		mcp->menu = 0;
		xlog("executing integer function for %s with %d", item->name, item->index);
		(*item->ifunc)(item->index);
		return;
	}
}

static void menu_paint() {
	if (menu->config) {
		switch (menu->config->style) {

		case bits:
			menu_get_selected();
			// bitwise input > print current value as bits
			mvwprintw(menu->cwindow, 3, XPOS_BITS, "%s", printBits(current));
			wmove(menu->cwindow, YPOS_BITS, XPOS_BITS + 7 - xpos);
			if (xpos >= 0 && xpos <= 7) {
				// highlight position if cursor is inside
				wchgat(menu->cwindow, 1, A_REVERSE | A_BOLD, 0, NULL);
			}
			break;

		case value:
			menu_get_selected();
			// direct input -> print current value as integer
			mvwprintw(menu->cwindow, YPOS_VALUE, XPOS_VALUE, "%03d", current);
			break;

		case selection:
			menu_get_selected();
			// we have items and configuration -> mark current as not selectable
			for (ITEM **citem = menu->cmenu->items; *citem; citem++) {
				const menuitem_t *item = item_userptr(*citem);
				if (item && item->index == current) {
					item_opts_off(*citem, O_SELECTABLE);
				} else {
					item_opts_on(*citem, O_SELECTABLE);
				}
			}
			break;

		case onoff:
			// read status for each item
			for (ITEM **citem = menu->cmenu->items; *citem; citem++) {
				const menuitem_t *item = item_userptr(*citem);
				const menuconfig_t *config = menu->config;
				if (item) {
					int state = (config->getfunc)(config, item);
					mvwprintw(menu->cwindow, (*citem)->index + 1, WIDTH - 3, "%1d", state);
				}
			}
			break;

		default:
			break;
		}
	}

	redrawwin(menu->cwindow);
	wrefresh(menu->cwindow);
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
		// TODO window is too small & overwriting borders
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
	xpos = 0;
	current = 0;

	// update screen
	xlog("painting '%s'", menu->title);
	menu_paint();
}

// !!! DO NOT use key names from linux/input.h - this breaks curses.h !!!
void menu_handle(int c) {
	if (!menu)
		return;

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

	// update screen
	menu_paint();
}
