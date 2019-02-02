#include "mcp.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

typedef void (*func)(void);

typedef struct menu_t {
    char *title;
    struct menu_t *back;
    const struct menuoption_t *items;
    int items_size;
} menu_t;

typedef struct menuoption_t {
    char *name;
    char *descr;
    menu_t *submenu;
    func fptr;
} menuoption_t;


/* Sub Menu 4 */
static const menuoption_t mo4[] = {
    { "Reboot", "Reboot", NULL, show_selection },
    { "Shutdown", "Shutdown", NULL, show_selection },
};
static menu_t m4 = { "System", NULL, mo4, ARRAY_SIZE(mo4) };


/* Sub Menu 3 */
static const menuoption_t mo3[] = {
    { "2.1", "(1)", NULL, show_selection },
    { "2.2", "(2)", NULL, show_selection },
    { "2.3", "(3)", NULL, show_selection },
    { "2.4", "(4)", NULL, show_selection },
};
static menu_t m3 = { "Sub Menu 3", NULL, mo3, ARRAY_SIZE(mo3) };


/* Sub Menu 2 */
static const menuoption_t mo2[] = {
    { "2.1", "(1)", NULL, show_selection },
    { "2.2", "(2)", NULL, show_selection },
    { "2.3", "(3)", NULL, show_selection },
    { "2.4", "(4)", NULL, show_selection },
};
static menu_t m2 = { "Sub Menu 2", NULL, mo2, ARRAY_SIZE(mo2) };


/* Sub Menu 1 */
static const menuoption_t mo1[] = {
    { "1.1", "(1)", NULL, show_selection },
    { "1.2", "(2)", NULL, show_selection },
    { "1.3", "(3)", NULL, show_selection },
    { "1.4", "(4)", NULL, show_selection },
};
static menu_t m1 = { "Sub Menu 1", NULL, mo1, ARRAY_SIZE(mo1) };


/* Main Menu */
static const menuoption_t mo0[] = {
    { "Stop/Play", "", &m1, NULL },
    { "Playlist", "", &m2, NULL },
    { "xxx1", "", &m3, NULL },
    { "xxx2", "", &m3, NULL },
    { "xxx3", "", &m3, NULL },
    { "xxx4", "", &m3, NULL },
    { "System", "", &m4, NULL },
};
static menu_t m0 = { "Main Menu", NULL, mo0, ARRAY_SIZE(mo0) };


void menu_back_connect() {
	m1.back = &m0;
	m2.back = &m0;
	m3.back = &m0;
	m4.back = &m0;
}
