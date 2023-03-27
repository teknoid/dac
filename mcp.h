#include <mpd/status.h>

#define TRON
// #define ANUS
// #define PIWOLF
// #define SABRE18
// #define SABRE28

#ifdef TRON
#define MUSIC 			"/music/"
#define I2C				"/dev/i2c-11"
#endif

#ifdef ANUS
#define MUSIC 			"/opt/music/"
#define DISPLAY			"/dev/tty"
#endif

#ifdef PIWOLF
#define MUSIC 			"/var/lib/mpd/music/"
#define DEVINPUT_IR		"/dev/input/infrared"
#endif

#ifdef SABRE18
#define MUSIC 			"/var/lib/mpd/music/"
#define DEVINPUT_IR		"/dev/input/infrared"
#endif

#ifdef SABRE28
#define MUSIC 			"/var/lib/mpd/music/"
#define DEVINPUT_IR		"/dev/input/infrared"
#define DEVINPUT_RA		"/dev/input/rotary_axis"
#define DEVINPUT_RB		"/dev/input/rotary_button"
#define DISPLAY			"/dev/tty1"
#define I2C				"/dev/i2c-0"
#endif

#define EXTERNAL 		"/usr/local/bin/mcp-external.sh"

#define BUFSIZE			256

// register a module in the MCP's execution context
#define MCP_REGISTER(name, prio, init, stop) \
  void __attribute__((constructor(101 + prio))) \
  register_##name(void) { mcp_register("\""#name"\"", init, stop); };

typedef enum {
	nlock, dsd, pcm, spdif, dop
} dac_signal_t;

typedef enum {
	mpd, opt, coax
} dac_source_t;

typedef int (*init_t)();
typedef void (*stop_t)();
typedef struct mcp_module_t {
	const char *name;
	init_t init;
	stop_t stop;
	void *next;
} mcp_module_t;

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

int mcp_status_get(const void*, const void*);
void mcp_status_set(const void*, const void*, int);
void mcp_system_shutdown(void);
void mcp_system_reboot(void);
void mcp_register(const char*, const void*, const void*);
