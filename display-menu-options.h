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
    { "DAC on/off",		NULL, NULL, dac_power, NULL, 0 },
    { "Reboot",			NULL, NULL, system_reboot, NULL, 0 },
    { "Shutdown",		NULL, NULL, system_shutdown, NULL, 0 },
};
static menu_t m_system = { "System", NULL, mi_system, ARRAY_SIZE(mi_system) };


/* Input Selection Menu */
static const menuitem_t mi_input[] = {
    { "I2S", 			NULL, NULL, NULL, show_selection, 1 },
    { "Optical",		NULL, NULL, NULL, show_selection, 2 },
    { "Coax",			NULL, NULL, NULL, show_selection, 3 },
};
static menu_t m_input = { "Input", NULL, mi_input, ARRAY_SIZE(mi_input) };


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
static menu_t m_playlist = { "Playlist", NULL, mi_playlist, ARRAY_SIZE(mi_playlist) };


/* Main Menu */
static const menuitem_t mi_main[] = {
    { "Stop/Play", 		NULL, NULL, NULL, mpdclient_handle, 119 },
    { "Playlist", 		NULL, &m_playlist, NULL, NULL, 0 },
    { "Input",			NULL, &m_input, NULL, NULL, 0 },
    { "System",			NULL, &m_system, NULL, NULL, 0 },
};
static menu_t m_main = { "Main Menu", NULL, mi_main, ARRAY_SIZE(mi_main) };


void menu_setup() {
	m_playlist.back 	= &m_main;
	m_input.back 		= &m_main;
	m_system.back 		= &m_main;
}
