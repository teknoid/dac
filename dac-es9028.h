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
 * DAC Settings configuration
 */

static const label_value_t filter_values[] = {
		{ "1", 1 },
		{ "2", 2 },
		{ "3", 3 },
};
static setting_t lv_filters = { " Filters ", 0x22, 0xf0, filter_values };

/**
 * Menu structure
 */

/* System Menu */
static const menuitem_t mi_system[] = {
    { "DAC on/off", 	NULL, NULL, dac_power, NULL, 0 },
    { "Reboot", 		NULL, NULL, system_reboot, NULL, 0 },
    { "Shutdown", 		NULL, NULL, system_shutdown, NULL, 0 },
};
static menu_t m_system = { " System ", NULL, mi_system, ARRAY_SIZE(mi_system), NULL, NULL };


/* Input Selection Menu */
static const menuitem_t mi_input[] = {
    { "MPD", 			NULL, NULL, NULL, dac_source, mpd },
    { "Optical", 		NULL, NULL, NULL, dac_source, opt },
    { "Coax", 			NULL, NULL, NULL, dac_source, coax },
};
static menu_t m_input = { " Input ", NULL, mi_input, ARRAY_SIZE(mi_input), NULL, NULL };


/* PlayList Menu */
static const menuitem_t mi_playlist[] = {
    { "00 incoming", 	NULL, NULL, NULL, mpdclient_handle, 11 },
    { "01 top", 		NULL, NULL, NULL, mpdclient_handle, 2 },
    { "02 aktuell", 	NULL, NULL, NULL, mpdclient_handle, 3 },
    { "03 modern", 		NULL, NULL, NULL, mpdclient_handle, 4 },
    { "04 eurodance", 	NULL, NULL, NULL, mpdclient_handle, 5 },
    { "05 umz", 		NULL, NULL, NULL, mpdclient_handle, 6 },
    { "06 extended", 	NULL, NULL, NULL, mpdclient_handle, 7 },
    { "07 classics", 	NULL, NULL, NULL, mpdclient_handle, 8 },
    { "08 slow", 		NULL, NULL, NULL, mpdclient_handle, 9 },
    { "09 movie", 		NULL, NULL, NULL, mpdclient_handle, 10 },
};
static menu_t m_playlist = { " Playlist ", NULL, mi_playlist, ARRAY_SIZE(mi_playlist), NULL, NULL };


/* Main Menu */
static const menuitem_t mi_main[] = {
    { "Stop/Play", 		NULL, NULL, NULL, mpdclient_handle, 119 },
    { "Playlist", 		NULL, &m_playlist, NULL, NULL, 0 },
    { "Input", 			NULL, &m_input, NULL, NULL, 0 },
    { "System", 		NULL, &m_system, NULL, NULL, 0 },
};
static menu_t m_main = { " Main Menu ", NULL, mi_main, ARRAY_SIZE(mi_main), NULL, NULL };
