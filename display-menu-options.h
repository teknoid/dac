#include "mcp.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

typedef void (*func_t)(void);
typedef void (*ifunc_t)(int);

typedef struct menu_t {
    char *title;
    struct menu_t *back;
    const struct menuitem_t *items;
    int items_size;
} menu_t;

typedef struct menuitem_t {
    char *name;
    char *descr;
    menu_t *submenu;
    func_t func;
    ifunc_t ifunc;
    int ifunc_arg;
} menuitem_t;


/* System Menu */
static const menuitem_t mi_system[] = {
    { "DAC on/off",	NULL, NULL, dac_power, NULL, 0 },
    { "Reboot",		NULL, NULL, system_reboot, NULL, 0 },
    { "Shutdown",	NULL, NULL, system_shutdown, NULL, 0 },
};
static menu_t m_system = { "System", NULL, mi_system, ARRAY_SIZE(mi_system) };


/* Sub Menu 3 */
static const menuitem_t mi3[] = {
    { "2.1", "(1)", NULL, NULL, show_selection, 1 },
    { "2.2", "(2)", NULL, NULL, show_selection, 2 },
    { "2.3", "(3)", NULL, NULL, show_selection, 3 },
    { "2.4", "(4)", NULL, NULL, show_selection, 4 },
};
static menu_t m3 = { "Sub Menu 3", NULL, mi3, ARRAY_SIZE(mi3) };


/* Sub Menu 2 */
static const menuitem_t mi2[] = {
    { "2.1", "(1)", NULL, NULL, show_selection, 1 },
    { "2.2", "(2)", NULL, NULL, show_selection, 2 },
    { "2.3", "(3)", NULL, NULL, show_selection, 3 },
    { "2.4", "(4)", NULL, NULL, show_selection, 4 },
};
static menu_t m2 = { "Sub Menu 2", NULL, mi2, ARRAY_SIZE(mi2) };


/* PlayList Menu */
static const menuitem_t mi_playlist[] = {
    { "00 incoming",	NULL, NULL, NULL, mpdclient_handle, 11 },
    { "01 top", 		NULL, NULL, NULL, mpdclient_handle, 2 },
    { "02 aktuell",		NULL, NULL, NULL, mpdclient_handle, 3 },
    { "03 modern",		NULL, NULL, NULL, mpdclient_handle, 4 },
    { "04 eurodance",	NULL, NULL, NULL, mpdclient_handle, 5 },
    { "05 umz",			NULL, NULL, NULL, mpdclient_handle, 6 },
    { "06 extended",	NULL, NULL, NULL, mpdclient_handle, 7 },
    { "07 classics",	NULL, NULL, NULL, mpdclient_handle, 8 },
    { "08 slow",		NULL, NULL, NULL, mpdclient_handle, 9 },
    { "09 movie",		NULL, NULL, NULL, mpdclient_handle, 10 },
};
static menu_t m_playlist = { "Sub Menu 1", NULL, mi_playlist, ARRAY_SIZE(mi_playlist) };


/* Main Menu */
static const menuitem_t mi_main[] = {
    { "Play/Pause", 	NULL, NULL, NULL, mpdclient_handle, 119 },
    { "Playlist", 		NULL, &m_playlist, NULL, NULL, 0 },
    { "xxx1",			NULL, &m3, NULL, NULL, 0 },
    { "xxx2",			NULL, &m3, NULL, NULL, 0 },
    { "xxx3",			NULL, &m3, NULL, NULL, 0 },
    { "xxx4",			NULL, &m3, NULL, NULL, 0 },
    { "System",			NULL, &m_system, NULL, NULL, 0 },
};
static menu_t m_main = { "Main Menu", NULL, mi_main, ARRAY_SIZE(mi_main) };


void menu_setup() {
	m_playlist.back = &m_main;
	m2.back = &m_main;
	m3.back = &m_main;
	m_system.back = &m_main;
}
