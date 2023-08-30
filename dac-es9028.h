//
// inspired by dimdims TFT HiFiDuino Pro Project
// http://www.dimdim.gr/tft-hifiduino-pro-project/
//

#define GPIO_EXT_POWER		"PA0"
#define GPIO_DAC_POWER		"PG11"

#define GPIO_SWITCH2		"PA1"
#define GPIO_SWITCH3		"PA2"
#define GPIO_SWITCH4		"PA3"

#define DEFAULT_VOLUME		0x60

#define ADDR				0x48

#define MCLK				100000000

#define	REG_SYSTEM			0x00
#define REG_INPUT			0x01
#define REG_MUTE			0x07
#define REG_SOURCE			0x0b
#define REG_CONFIG			0x0f
#define REG_VOLUME			0x10
#define REG_STATUS			0x40
#define REG_SIGNAL			0x64

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
    { 8, 	"07 chill",			NULL, NULL, NULL, mpdclient_handle },
    { 9, 	"08 slow",			NULL, NULL, NULL, mpdclient_handle },
    { 10, 	"09 house",			NULL, NULL, NULL, mpdclient_handle },
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
    { 0,	"Reboot",			NULL, NULL, mcp_system_reboot, NULL },
    { 0,	"Shutdown",			NULL, NULL, mcp_system_shutdown, NULL },
};
static menu_t m_system = { "System", "System Operations", NULL, NULL, mi_system, ARRAY_SIZE(mi_system), NULL, NULL };


/* Status Menu */
static const menuconfig_t mc_status = { onoff, mcp_status_get, mcp_status_set, 0, 0, 0, 0, 0 };
static const menuitem_t mi_status[] = {
    { 1,	"IR on/off",		NULL, NULL, NULL, NULL },
};
static menu_t m_status = { "Status", "Status changes", NULL, &mc_status, mi_status, ARRAY_SIZE(mi_status), NULL, NULL };


/* Filter Type Menu */
static const menuconfig_t mc_filter = { selection, dac_status_get, dac_status_set, 7, 0b11100000, 0, 7, 2 };
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
static const menuconfig_t mc_iir = { selection, dac_status_get, dac_status_set, 7, 0b00000110, 0, 3, 0 };
static const menuitem_t mi_iir[] = {
	{ 0,	"47k @ 44.1kHz",	NULL, NULL, NULL, NULL },
	{ 1,	"50k @ 44.1kHz",	NULL, NULL, NULL, NULL },
	{ 2,	"60k @ 44.1kHz",	NULL, NULL, NULL, NULL },
	{ 3,	"70k @ 44.1kHz",	NULL, NULL, NULL, NULL },
};
static menu_t m_iir = { "IIR Bandwidth", "Selects the type of filter to use during the 8x IIR interpolation phase", NULL, &mc_iir, mi_iir, ARRAY_SIZE(mi_iir), NULL, NULL };


/* Jitter Eliminator / DPLL Bandwidth in I2S+SPDIF mode */
static const menuconfig_t mc_dpll_spdif = { bits, dac_status_get, dac_status_set, 12, 0b11110000, 0, 15, 5 };
static menu_t m_dpll_spdif = { "DPLL I2S/SPDIF", "Sets the bandwidth of the DPLL when operating in I2S/SPDIF mode", NULL, &mc_dpll_spdif, NULL, 0, NULL, NULL };


/* Jitter Eliminator / DPLL Bandwidth in DSD mode */
static const menuconfig_t mc_dpll_dsd = { bits, dac_status_get, dac_status_set, 12, 0b00001111, 0, 15, 10 };
static menu_t m_dpll_dsd = { "DPLL DSD", "Sets the bandwidth of the DPLL when operating in DSD mode", NULL, &mc_dpll_dsd, NULL, 0, NULL, NULL };


/* Lock Speed */
static const menuconfig_t mc_lock_speed = { selection, dac_status_get, dac_status_set, 10, 0b00001111, 0, 15, 0 };
static const menuitem_t mi_lock_speed[] = {
	{ 0,	"16384 FSL edges",	NULL, NULL, NULL, NULL },
	{ 1,	" 8192 FSL edges",	NULL, NULL, NULL, NULL },
	{ 2,	" 5461 FSL edges",	NULL, NULL, NULL, NULL },
	{ 3,	" 4096 FSL edges",	NULL, NULL, NULL, NULL },
	{ 4,	" 3276 FSL edges",	NULL, NULL, NULL, NULL },
	{ 5,	" 2730 FSL edges",	NULL, NULL, NULL, NULL },
	{ 6,	" 2340 FSL edges",	NULL, NULL, NULL, NULL },
	{ 7,	" 2048 FSL edges",	NULL, NULL, NULL, NULL },
	{ 8,	" 1820 FSL edges",	NULL, NULL, NULL, NULL },
	{ 9,	" 1638 FSL edges",	NULL, NULL, NULL, NULL },
	{ 10,	" 1489 FSL edges",	NULL, NULL, NULL, NULL },
	{ 11,	" 1365 FSL edges",	NULL, NULL, NULL, NULL },
	{ 12,	" 1260 FSL edges",	NULL, NULL, NULL, NULL },
	{ 13,	" 1170 FSL edges",	NULL, NULL, NULL, NULL },
	{ 14,	" 1092 FSL edges",	NULL, NULL, NULL, NULL },
	{ 15,	" 1024 FSL edges",	NULL, NULL, NULL, NULL },
};
static menu_t m_lock_speed = { "Lock Speed", "Sets the number of audio samples required before the DPLL and jitter eliminator lock to the incoming signal.", NULL, &mc_lock_speed, mi_lock_speed, ARRAY_SIZE(mi_lock_speed), NULL, NULL };


/* Auto Mute */
static const menuconfig_t mc_automute = { selection, dac_status_get, dac_status_set, 2, 0b11000000, 0, 3, 0 };
static const menuitem_t mi_automute[] = {
	{ 0,	"normal",			NULL, NULL, NULL, NULL },
	{ 1,	"mute",				NULL, NULL, NULL, NULL },
	{ 2,	"ramp down",		NULL, NULL, NULL, NULL },
	{ 3,	"mute+ramp down",	NULL, NULL, NULL, NULL },
};
static menu_t m_automute = { "Auto Mute", "Configures the automute state machine, which allows the SABRE DAC to perform different power saving and sound optimizations.", NULL, &mc_automute, mi_automute, ARRAY_SIZE(mi_automute), NULL, NULL };

static const menuconfig_t mc_automute_time = { bits, dac_status_get, dac_status_set, 4, 0b11111111, 0, 255, 0 };
static menu_t m_automute_time = { "Auto Mute Time", "Configures the amount of time the audio data must remain below the automute_level before an automute condition is flagged. Defaults to 0 which disables automute.", NULL, &mc_automute_time, NULL, 0, NULL, NULL };

static const menuconfig_t mc_automute_level = { bits, dac_status_get, dac_status_set, 5, 0b01111111, 0, 127, 104 };
static menu_t m_automute_level = { "Auto Mute Level", "Configures the threshold which the audio must be below before an automute condition is flagged. The level is measured in decibels (dB) and defaults to -104dB.", NULL, &mc_automute_level, NULL, 0, NULL, NULL };

static const menuconfig_t mc_18db_gain = { bits, dac_status_get, dac_status_set, 62, 0b11111111, 0, 0, 0 };
static menu_t m_18db_gain = { "+18dB Gain", "+18dB gain applied after volume control. The +18dB gain only works in PCM mode and is applied prior to the channel mapping.", NULL, &mc_18db_gain, NULL, 0, NULL, NULL };

/* Setup Menu */
static const menuitem_t mi_setup[] = {
    { 0,	"Filter Shape",		NULL, &m_filter, NULL, NULL },
    { 0,	"IIR Bandwidth",	NULL, &m_iir, NULL, NULL },
    { 0,	"DPLL I2S/SPDIF",	NULL, &m_dpll_spdif, NULL, NULL },
    { 0,	"DPLL DSD",			NULL, &m_dpll_dsd, NULL, NULL },
    { 0,	"Lock Speed",		NULL, &m_lock_speed, NULL, NULL },
    { 0,	"Auto Mute",		NULL, &m_automute, NULL, NULL },
    { 0,	"Auto Mute Time",	NULL, &m_automute_time, NULL, NULL },
    { 0,	"Auto Mute Level",	NULL, &m_automute_level, NULL, NULL },
    { 0,	"+18dB Gain",		NULL, &m_18db_gain, NULL, NULL },
};
static menu_t m_setup = { "Setup", "Configure DAC Settings", NULL, NULL, mi_setup, ARRAY_SIZE(mi_setup), NULL, NULL };


/* Main Menu */
static const menuitem_t mi_main[] = {
    { 119,	"Stop/Play",		NULL, NULL, NULL, mpdclient_handle },
    { 0,	"Playlist",			NULL, &m_playlist, NULL, NULL },
    { 1,	"Input",			NULL, &m_input, NULL, NULL },
    { 2,	"Setup",			NULL, &m_setup, NULL, NULL },
    { 3,	"System",			NULL, &m_system, NULL, NULL },
    { 4,	"Status",			NULL, &m_status, NULL, NULL },
};
static menu_t m_main = { "Main Menu", "", NULL, NULL, mi_main, ARRAY_SIZE(mi_main), NULL, NULL };

// create and connect the menus
static void es9028_prepare_menus() {
	menu_create(&m_main, NULL);
	menu_create(&m_playlist, &m_main);
	menu_create(&m_input, &m_main);
	menu_create(&m_setup, &m_main);
	menu_create(&m_system, &m_main);
	menu_create(&m_status, &m_main);
	menu_create(&m_filter, &m_setup);
	menu_create(&m_iir, &m_setup);
	menu_create(&m_dpll_spdif, &m_setup);
	menu_create(&m_dpll_dsd, &m_setup);
	menu_create(&m_lock_speed, &m_setup);
	menu_create(&m_automute, &m_setup);
	menu_create(&m_automute_time, &m_setup);
	menu_create(&m_automute_level, &m_setup);
	menu_create(&m_18db_gain, &m_setup);
}
