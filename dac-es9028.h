#include "mcp.h"
#include "display.h"
#include "display-menu.h"

#define GPIO_EXT_POWER		0
#define GPIO_DAC_POWER		7

#define GPIO_LAMP			3

#define ADDR				0x48
#define	REG_SYSTEM			0x00
#define REG_INPUT			0x01
#define REG_FILTER_MUTE		0x07
#define REG_SOURCE			0x0b
#define REG_CONFIG			0x0f
#define REG_VOLUME			0x10
#define REG_STATUS			0x40
#define REG_SIGNAL			0x64

#define DEFAULT_VOLUME		0x60
#define MCLK				100000000

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

/**
 * Menu structure
 */

/* PlayList Menu */
static const menuitem_t mi_playlist[] = {
    { 11, 	"00 incoming",	NULL, NULL, NULL, mpdclient_handle },
    { 2, 	"01 top",		NULL, NULL, NULL, mpdclient_handle },
    { 3, 	"02 aktuell",	NULL, NULL, NULL, mpdclient_handle },
    { 4, 	"03 modern",	NULL, NULL, NULL, mpdclient_handle },
    { 5, 	"04 eurodance",	NULL, NULL, NULL, mpdclient_handle },
    { 6, 	"05 umz",		NULL, NULL, NULL, mpdclient_handle },
    { 7, 	"06 extended",	NULL, NULL, NULL, mpdclient_handle },
    { 8, 	"07 classics",	NULL, NULL, NULL, mpdclient_handle },
    { 9, 	"08 slow",		NULL, NULL, NULL, mpdclient_handle },
    { 10, 	"09 movie",		NULL, NULL, NULL, mpdclient_handle },
};
static menu_t m_playlist = { " Playlist ", NULL, NULL, mi_playlist, ARRAY_SIZE(mi_playlist), NULL, NULL };


/* Input Selection Menu */
static const menuitem_t mi_input[] = {
    { mpd,	"MPD",			NULL, NULL, NULL, dac_source },
    { opt,	"Optical",		NULL, NULL, NULL, dac_source },
    { coax,	"Coax",			NULL, NULL, NULL, dac_source },
};
static menu_t m_input = { " Input ", NULL, NULL, mi_input, ARRAY_SIZE(mi_input), NULL, NULL };


/* System Menu */
static const menuitem_t mi_system[] = {
    { 0,	"DAC on/off",	NULL, NULL, dac_power, NULL },
    { 0,	"Reboot",		NULL, NULL, system_reboot, NULL },
    { 0,	"Shutdown",		NULL, NULL, system_shutdown, NULL },
};
static menu_t m_system = { " System ", NULL, NULL, mi_system, ARRAY_SIZE(mi_system), NULL, NULL };


/* Filter Menu */
static const menuconfig_t mc_filter = { dac_config_get, dac_config_set, 0x23, 0xf0 };
static const menuitem_t mi_filter[] = {
	{ 1,	"1",			NULL, NULL, NULL, NULL},
	{ 2,	"2",			NULL, NULL, NULL, NULL},
	{ 3,	"3",			NULL, NULL, NULL, NULL},
};
static menu_t m_filter 	= { " Filters ", NULL, &mc_filter, mi_filter, ARRAY_SIZE(mi_filter), NULL, NULL };


/* IIR Menu */
static const menuconfig_t mc_iir = { dac_config_get, dac_config_set, 0x42, 0x0f };
static const menuitem_t mi_iir[] = {
	{ 4,	"4",			NULL, NULL, NULL, NULL},
	{ 5,	"5",			NULL, NULL, NULL, NULL},
	{ 6,	"6",			NULL, NULL, NULL, NULL},
};
static menu_t m_iir 	= { " IIR ", NULL, &mc_iir, mi_iir, ARRAY_SIZE(mi_iir), NULL, NULL };


/* DPLL Menu */
static const menuconfig_t mc_dpll = { dac_config_get, dac_config_set, 0x88, 0x07 };
static const menuitem_t mi_dpll[] = {
	{ 7,	"7",			NULL, NULL, NULL, NULL},
	{ 8,	"8",			NULL, NULL, NULL, NULL},
	{ 9,	"9",			NULL, NULL, NULL, NULL},
};
static menu_t m_dpll 	= { " DPLL ", NULL, &mc_dpll, mi_dpll, ARRAY_SIZE(mi_dpll), NULL, NULL };


/* Setup Menu */
static const menuitem_t mi_setup[] = {
    { 0,	"Filter",		NULL, &m_filter, NULL, NULL },
    { 0,	"IIR",			NULL, &m_iir, NULL, NULL },
    { 0,	"DPLL",			NULL, &m_dpll, NULL, NULL },
};
static menu_t m_setup = { " Setup ", NULL, NULL, mi_setup, ARRAY_SIZE(mi_setup), NULL, NULL };


/* Main Menu */
static const menuitem_t mi_main[] = {
    { 119,	"Stop/Play",	NULL, NULL, NULL, mpdclient_handle },
    { 0,	"Playlist",		NULL, &m_playlist, NULL, NULL },
    { 1,	"Input",		NULL, &m_input, NULL, NULL },
    { 2,	"Setup",		NULL, &m_setup, NULL, NULL },
    { 3,	"System",		NULL, &m_system, NULL, NULL },
};
static menu_t m_main = { " Main Menu ", NULL, NULL, mi_main, ARRAY_SIZE(mi_main), NULL, NULL };

// create and connect the menus
static dac_prepeare_menus() {
	menu_create(&m_main, NULL);
	menu_create(&m_playlist, &m_main);
	menu_create(&m_input, &m_main);
	menu_create(&m_setup, &m_main);
	menu_create(&m_system, &m_main);
	menu_create(&m_filter, &m_setup);
	menu_create(&m_iir, &m_setup);
	menu_create(&m_dpll, &m_setup);
}
