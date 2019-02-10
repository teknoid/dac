#include <curses.h>
#include <menu.h>

typedef void (*func_t)(void);
typedef void (*ifunc_t)(int);

/**
 * Settings data structures
 */
typedef struct label_value_t {
	char *label;
	int value;
} label_value_t;

typedef struct setting_t {
	char *title;
	int reg;
	int mask;
	const struct label_value_t *values;
} setting_t;

/**
 * Menu data structures
 */
typedef struct menu_t {
	char *title;
	struct menu_t *back;
	const struct menuitem_t *items;
	int items_size;
	MENU *cmenu;
	WINDOW *cwindow;
} menu_t;

typedef struct menuitem_t {
	char *name;
	char *descr;
	menu_t *submenu;
	func_t func;
	ifunc_t ifunc;
	int ifunc_arg;
} menuitem_t;

void menu_create(menu_t *, menu_t *);
void menu_open(menu_t *);
void menu_handle(int);
void menu_show_selection(int);
