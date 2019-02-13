#include <stddef.h>

//
// based on inspirations from dimdims TFT HiFiDuino Pro Project
// http://www.dimdim.gr/tft-hifiduino-pro-project/
//

#define ADDR				0x48

#define MCLK				100000000

#define	REG_SYSTEM			0x00
#define REG_INPUT			0x01
#define REG_FILTER_MUTE		0x07
#define REG_SOURCE			0x0b
#define REG_CONFIG			0x0f
#define REG_VOLUME			0x10
#define REG_STATUS			0x40
#define REG_SIGNAL			0x64

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

/**
 * Menu structure
 */

/* PlayList Menu */
static const menuitem_t mi_playlist[] = {
    { 11, 	"00 incoming",		NULL, NULL, NULL, mpdclient_handle },
    { 2, 	"01 top",			NULL, NULL, NULL, mpdclient_handle },
    { 3, 	"02 aktuell",		NULL, NULL, NULL, mpdclient_handle },
    { 4, 	"03 modern",		NULL, NULL, NULL, mpdclient_handle },
    { 5, 	"04 eurodance",		NULL, NULL, NULL, mpdclient_handle },
    { 6, 	"05 umz",			NULL, NULL, NULL, mpdclient_handle },
    { 7, 	"06 extended",		NULL, NULL, NULL, mpdclient_handle },
    { 8, 	"07 classics",		NULL, NULL, NULL, mpdclient_handle },
    { 9, 	"08 slow",			NULL, NULL, NULL, mpdclient_handle },
    { 10, 	"09 movie",			NULL, NULL, NULL, mpdclient_handle },
};
static menu_t m_playlist = { "Playlist", "Load a MPD Playlist", NULL, NULL, mi_playlist, ARRAY_SIZE(mi_playlist), NULL, NULL };


/* Input Selection Menu */
static const menuitem_t mi_input[] = {
    { mpd,	"MPD",				NULL, NULL, NULL, dac_source },
    { opt,	"Optical",			NULL, NULL, NULL, dac_source },
    { coax,	"Coax",				NULL, NULL, NULL, dac_source },
};
static menu_t m_input = { "Input", "Set DAC Input Source", NULL, NULL, mi_input, ARRAY_SIZE(mi_input), NULL, NULL };


/* System Menu */
static const menuitem_t mi_system[] = {
    { 0,	"DAC on/off",		NULL, NULL, dac_power, NULL },
    { 0,	"Reboot",			NULL, NULL, system_reboot, NULL },
    { 0,	"Shutdown",			NULL, NULL, system_shutdown, NULL },
};
static menu_t m_system = { "System", "System Operations", NULL, NULL, mi_system, ARRAY_SIZE(mi_system), NULL, NULL };


/* Filter Type Menu */
static const menuconfig_t mc_filter = { dac_config_get, dac_config_set, 7, 0b11100000, 2 };
static const menuitem_t mi_filter[] = {
	{ 0,	"Fast/Linear",		"Fast Roll-off, Linear Phase Filter", NULL, NULL, NULL },
	{ 1,	"Slow/Linear",		"Slow Roll-off, Linear Phase Filter", NULL, NULL, NULL },
	{ 2,	"Fast/Minimum",		"Fast Roll-off, Minimum Phase Filter", NULL, NULL, NULL },
	{ 3,	"Slow/Minimum",		"Slow Roll-off, Minimum Phase", NULL, NULL, NULL },
	{ 5,	"Apodizing",		"Apodizing, Fast Roll-off, Linear Phase", NULL, NULL, NULL },
	{ 6,	"Hybrid",			"Hybrid, Fast Roll-off, Minimum Phase", NULL, NULL, NULL },
	{ 7,	"Brickwall",		"Brickwall Filter", NULL, NULL, NULL },
};
static menu_t m_filter = { "Filter Shape", "Selects the type of filter to use during the 8x FIR interpolation phase", NULL, &mc_filter, mi_filter, ARRAY_SIZE(mi_filter), NULL, NULL };


/* IIR Bandwidth Menu */
static const menuconfig_t mc_iir = { dac_config_get, dac_config_set, 7, 0b00000110, 0 };
static const menuitem_t mi_iir[] = {
	{ 0,	"47k @ 44.1kHz",	NULL, NULL, NULL, NULL },
	{ 1,	"50k @ 44.1kHz",	NULL, NULL, NULL, NULL },
	{ 2,	"60k @ 44.1kHz",	NULL, NULL, NULL, NULL },
	{ 3,	"70k @ 44.1kHz",	NULL, NULL, NULL, NULL },
};
static menu_t m_iir = { "IIR Bandwidth", "Selects the type of filter to use during the 8x IIR interpolation phase", NULL, &mc_iir, mi_iir, ARRAY_SIZE(mi_iir), NULL, NULL };


/* Jitter Eliminator / DPLL Bandwidth in I2S / SPDIF mode */
static const menuconfig_t mc_dpll_spdif = { dac_config_get, dac_config_set, 12, 0b11110000, 5 };
static const menuitem_t mi_dpll_spdif[] = {
	{ 0,	"0",				NULL, NULL, NULL, NULL },
	{ 1,	"1",				NULL, NULL, NULL, NULL },
	{ 2,	"2",				NULL, NULL, NULL, NULL },
	{ 3,	"3",				NULL, NULL, NULL, NULL },
	{ 4,	"4",				NULL, NULL, NULL, NULL },
	{ 5,	"5",				NULL, NULL, NULL, NULL },
	{ 6,	"6",				NULL, NULL, NULL, NULL },
	{ 7,	"7",				NULL, NULL, NULL, NULL },
	{ 8,	"8",				NULL, NULL, NULL, NULL },
	{ 9,	"9",				NULL, NULL, NULL, NULL },
	{ 10,	"10",				NULL, NULL, NULL, NULL },
	{ 11,	"11",				NULL, NULL, NULL, NULL },
	{ 12,	"12",				NULL, NULL, NULL, NULL },
	{ 13,	"13",				NULL, NULL, NULL, NULL },
	{ 14,	"14",				NULL, NULL, NULL, NULL },
	{ 15,	"15",				NULL, NULL, NULL, NULL },
};
static menu_t m_dpll_spdif = { "DPLL I2S/SPDIF", "Sets the bandwidth of the DPLL when operating in I2S/SPDIF mode", NULL, &mc_dpll_spdif, mi_dpll_spdif, ARRAY_SIZE(mi_dpll_spdif), NULL, NULL };

/* Jitter Eliminator / DPLL Bandwidth in DSD mode */
static const menuconfig_t mc_dpll_dsd = { dac_config_get, dac_config_set, 12, 0b00001111, 10 };
static const menuitem_t mi_dpll_dsd[] = {
	{ 0,	"0",				NULL, NULL, NULL, NULL },
	{ 1,	"1",				NULL, NULL, NULL, NULL },
	{ 2,	"2",				NULL, NULL, NULL, NULL },
	{ 3,	"3",				NULL, NULL, NULL, NULL },
	{ 4,	"4",				NULL, NULL, NULL, NULL },
	{ 5,	"5",				NULL, NULL, NULL, NULL },
	{ 6,	"6",				NULL, NULL, NULL, NULL },
	{ 7,	"7",				NULL, NULL, NULL, NULL },
	{ 8,	"8",				NULL, NULL, NULL, NULL },
	{ 9,	"9",				NULL, NULL, NULL, NULL },
	{ 10,	"10",				NULL, NULL, NULL, NULL },
	{ 11,	"11",				NULL, NULL, NULL, NULL },
	{ 12,	"12",				NULL, NULL, NULL, NULL },
	{ 13,	"13",				NULL, NULL, NULL, NULL },
	{ 14,	"14",				NULL, NULL, NULL, NULL },
	{ 15,	"15",				NULL, NULL, NULL, NULL },
};
static menu_t m_dpll_dsd = { "DPLL DSD", "Sets the bandwidth of the DPLL when operating in DSD mode", NULL, &mc_dpll_dsd, mi_dpll_dsd, ARRAY_SIZE(mi_dpll_dsd), NULL, NULL };


/* Setup Menu */
static const menuitem_t mi_setup[] = {
    { 0,	"Filter Shape",		NULL, &m_filter, NULL, NULL },
    { 0,	"IIR Bandwidth",	NULL, &m_iir, NULL, NULL },
    { 0,	"DPLL I2S/SPDIF",	NULL, &m_dpll_spdif, NULL, NULL },
    { 0,	"DPLL DSD",			NULL, &m_dpll_dsd, NULL, NULL },
};
static menu_t m_setup = { "Setup", "Configure DAC Settings", NULL, NULL, mi_setup, ARRAY_SIZE(mi_setup), NULL, NULL };


/* Main Menu */
static const menuitem_t mi_main[] = {
    { 119,	"Stop/Play",		NULL, NULL, NULL, mpdclient_handle },
    { 0,	"Playlist",			NULL, &m_playlist, NULL, NULL },
    { 1,	"Input",			NULL, &m_input, NULL, NULL },
    { 2,	"Setup",			NULL, &m_setup, NULL, NULL },
    { 3,	"System",			NULL, &m_system, NULL, NULL },
};
static menu_t m_main = { "Main Menu", "", NULL, NULL, mi_main, ARRAY_SIZE(mi_main), NULL, NULL };

// create and connect the menus
static void es9028_prepeare_menus() {
	menu_create(&m_main, NULL);
	menu_create(&m_playlist, &m_main);
	menu_create(&m_input, &m_main);
	menu_create(&m_setup, &m_main);
	menu_create(&m_system, &m_main);
	menu_create(&m_filter, &m_setup);
	menu_create(&m_iir, &m_setup);
	menu_create(&m_dpll_spdif, &m_setup);
	menu_create(&m_dpll_dsd, &m_setup);
}
