#define TRON
// #define ANUS
// #define PIWOLF
// #define SABRE18
// #define SABRE28

#ifdef TRON
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/music/"
#define I2C				"/dev/i2c-11"
#endif

#ifdef ANUS
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/opt/music/"
#define DISPLAY			"/dev/tty"
#endif

#ifdef PIWOLF
#define DEVINPUT_IR		"/dev/input/infrared"
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/var/lib/mpd/music/"
#endif

#ifdef SABRE18
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/var/lib/mpd/music/"
#define DEVINPUT_IR		"/dev/input/infrared"
#endif

#ifdef SABRE28
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/var/lib/mpd/music/"
#define DEVINPUT_IR		"/dev/input/infrared"
#define DEVINPUT_RA		"/dev/input/rotary_axis"
#define DEVINPUT_RB		"/dev/input/rotary_button"
#define DISPLAY			"/dev/tty1"
#define I2C				"/dev/i2c-0"
#endif

#define EXTERNAL 		"/usr/local/bin/mcp-external.sh"
#define MPD_HOST		"localhost"
#define MPD_PORT		6600

#define BUFSIZE			256

#define MCP_REGISTER(name, prio, init, destroy) void __attribute__((constructor(101 + prio))) register_##name(void) { mcp_register("\""#name"\"", init, destroy); };

#include <mpd/status.h>

typedef enum {
	nlock, dsd, pcm, spdif, dop
} dac_signal_t;

typedef enum {
	mpd, opt, coax
} dac_source_t;

typedef int (*init_t)();
typedef void (*destroy_t)();
typedef struct mcp_modules_t {
	const char *name;
	init_t init;
	destroy_t destroy;
	void *next;
} mcp_modules_t;

typedef struct mcp_state_t {
	dac_signal_t dac_signal;
	dac_source_t dac_source;
	int dac_state_changed;
	int ext_power;
	int dac_power;
	int dac_rate;
	int dac_volume;
	int dac_mute;
	enum mpd_state mpd_state;
	int mpd_bits;
	int mpd_rate;
	int clock_h;
	int clock_m;
	int nightmode;
	double load;
	double temp;
	int plist_key;
	int plist_pos;
	char artist[BUFSIZE];
	char title[BUFSIZE];
	char album[BUFSIZE];
	char extension[8];
	int menu;
	int ir_active;
	int switch1;
	int switch2;
	int switch3;
	int switch4;
} mcp_state_t;
extern mcp_state_t *mcp;

typedef struct mcp_config_t {
	int daemonize;
	int interactive;
} mcp_config_t;
extern mcp_config_t *cfg;

void dac_power(void);
void dac_mute(void);
void dac_unmute(void);
void dac_volume_up(void);
void dac_volume_down(void);
void dac_source(int);
void dac_handle(int);
int dac_status_get(const void*, const void*);
void dac_status_set(const void*, const void*, int);

void mpdclient_handle(int);

void replaygain(const char*);

int mcp_status_get(const void*, const void*);
void mcp_status_set(const void*, const void*, int);
void mcp_system_shutdown(void);
void mcp_system_reboot(void);
void mcp_register(const char*, const void*, const void*);
