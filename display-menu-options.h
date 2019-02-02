#include "mcp.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

typedef void (*func)(int);

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
    func fptr;
    int fptr_arg;
} menuitem_t;


/* System Menu */
static const menuitem_t mi_system[] = {
    { "Reboot",		NULL, NULL, system_reboot, 0 },
    { "Shutdown",	NULL, NULL, system_shutdown, 0 },
};
static menu_t m_system = { "System", NULL, mi_system, ARRAY_SIZE(mi_system) };


/* Sub Menu 3 */
static const menuitem_t mi3[] = {
    { "2.1", "(1)", NULL, show_selection, 1 },
    { "2.2", "(2)", NULL, show_selection, 2 },
    { "2.3", "(3)", NULL, show_selection, 3 },
    { "2.4", "(4)", NULL, show_selection, 4 },
};
static menu_t m3 = { "Sub Menu 3", NULL, mi3, ARRAY_SIZE(mi3) };


/* Sub Menu 2 */
static const menuitem_t mi2[] = {
    { "2.1", "(1)", NULL, show_selection, 1 },
    { "2.2", "(2)", NULL, show_selection, 2 },
    { "2.3", "(3)", NULL, show_selection, 3 },
    { "2.4", "(4)", NULL, show_selection, 4 },
};
static menu_t m2 = { "Sub Menu 2", NULL, mi2, ARRAY_SIZE(mi2) };


/* PlayList Menu */
static const menuitem_t mi_playlist[] = {
    { "00 incoming",	NULL, NULL, mpdclient_handle, 11 },
    { "01 top", 		NULL, NULL, mpdclient_handle, 2 },
    { "02 aktuell",		NULL, NULL, mpdclient_handle, 3 },
    { "03 modern",		NULL, NULL, mpdclient_handle, 4 },
    { "04 eurodance",	NULL, NULL, mpdclient_handle, 5 },
    { "05 umz",			NULL, NULL, mpdclient_handle, 6 },
    { "06 extended",	NULL, NULL, mpdclient_handle, 7 },
    { "07 classics",	NULL, NULL, mpdclient_handle, 8 },
    { "08 slow",		NULL, NULL, mpdclient_handle, 9 },
    { "09 movie",		NULL, NULL, mpdclient_handle, 10 },
};
static menu_t m_playlist = { "Sub Menu 1", NULL, mi_playlist, ARRAY_SIZE(mi_playlist) };


/* Main Menu */
static const menuitem_t mi_main[] = {
    { "Play/Pause", "", NULL, mpdclient_handle, 119 },
    { "Playlist", "", &m_playlist, NULL, 0 },
    { "xxx1", "", &m3, NULL, 0 },
    { "xxx2", "", &m3, NULL, 0 },
    { "xxx3", "", &m3, NULL, 0 },
    { "xxx4", "", &m3, NULL, 0 },
    { "System", "", &m_system, NULL, 0 },
};
static menu_t m_main = { "Main Menu", NULL, mi_main, ARRAY_SIZE(mi_main) };


void menu_setup() {
	m_playlist.back = &m_main;
	m2.back = &m_main;
	m3.back = &m_main;
	m_system.back = &m_main;
}
