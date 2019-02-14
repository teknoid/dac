#include <curses.h>
#include <menu.h>

/* Menu data structures */

//
// the menu itself - one window on the screen
//
typedef struct menu_t {
	const char *title;
	const char *descr;
	struct menu_t *back;
	const struct menuconfig_t *config;
	const struct menuitem_t *items;
	int size;
	MENU *cmenu;
	WINDOW *cwindow;
} menu_t;

//
// configuration menu - executing a function with selected item as parameter
//

typedef int (*getfunc_t)(const void *);

typedef void (*setfunc_t)(const void *, int);

typedef struct menuconfig_t {
	getfunc_t getfunc;
	setfunc_t setfunc;
	int reg;
	int mask;
	int min;
	int max;
	int def;
} menuconfig_t;

//
// execute a function (void or integer) for a selected item
//

typedef void (*vfunc_t)(void);

typedef void (*ifunc_t)(int);

typedef struct menuitem_t {
	int value;
	char *name;
	char *descr;
	menu_t *submenu;
	vfunc_t vfunc;
	ifunc_t ifunc;
} menuitem_t;

void menu_create(menu_t *, menu_t *);
void menu_open(menu_t *);
void menu_handle(int);
void menu_show_selection(int);
