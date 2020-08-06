#include <menu.h>

/*
 * Menu data structures
 */

// selection --> items must be filled
// value     --> no items but config->min and config-> max are set
// bits      --> config->min and config-> max are 0
typedef enum {
	selection, onoff, value, bits
} menu_style_t;

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

typedef int (*getfunc_t)(const void *, const void *);

typedef void (*setfunc_t)(const void *, const void *, int);

typedef struct menuconfig_t {
	menu_style_t style;
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
	int index;
	char *name;
	char *descr;
	menu_t *submenu;
	vfunc_t vfunc;
	ifunc_t ifunc;
} menuitem_t;

void menu_create(menu_t *, menu_t *);
void menu_open(menu_t *);
void menu_handle(int);
