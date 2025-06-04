#include <pthread.h>
#include <mpd/status.h>

// defaults for testing
#define WORK			"/work/simulator"
#define RUN				"/work/simulator"

//#define ANUS
//#define TRON
//#define ODROID
//#define PIWOLF
//#define PICAM
//#define SABRE18
//#define SABRE28

#ifdef ANUS
#define MUSIC 			"/opt/music/"
#define DISPLAY			"/dev/tty"
#endif

#ifdef TRON
#define FLAMINGO
#define SOLAR
#define LCD
#define I2C				"/dev/i2c-7"
#define MIXER			"/usr/bin/amixer -q -D hw:CARD=USB2496play set PCM"
#endif

#ifdef ODROID
#define FLAMINGO
#define SOLAR
#endif

#ifdef PICAM
// /boot/firmware/config.txt: dtparam=i2c_vc=on
#define I2C				"/dev/i2c-0"
#endif

#ifdef PIWOLF
#define DEVINPUT_IR		"/dev/input/infrared"
#endif

#ifdef SABRE18
#define DAC
#define DEVINPUT_IR		"/dev/input/infrared"
#endif

#ifdef SABRE28
#define DAC
#define DEVINPUT_IR		"/dev/input/infrared"
#define DEVINPUT_RA		"/dev/input/rotary_axis"
#define DEVINPUT_RB		"/dev/input/rotary_button"
#define DISPLAY			"/dev/tty1"
#define I2C				"/dev/i2c-0"
#endif

#define EXTERNAL 		"/usr/local/bin/mcp-external.sh"

#define BUFSIZE			256

#ifndef WORK
#define WORK			"/work"
#endif

#ifndef RUN
#define RUN				"/run/mcp"
#endif

#ifndef TMP
#define TMP				"/tmp"
#endif

#define SLASH			"/"

// register a module in the MCP's execution context
#define MCP_REGISTER(name, prio, init, stop, loop) \
  void __attribute__((constructor(101 + prio))) \
  register_##name(void) { mcp_register("\""#name"\"", prio, init, stop, loop); };

typedef enum {
	nlock, dsd, pcm, spdif, dop
} dac_signal_t;

typedef enum {
	mpd, opt, coax
} dac_source_t;

typedef int (*init_t)();
typedef void (*stop_t)();
typedef void (*loop_t)();
typedef struct _mcp_module mcp_module_t;

struct _mcp_module {
	const char *name;
	init_t init;
	stop_t stop;
	loop_t loop;
	pthread_t thread;
	mcp_module_t *next;
};

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
	int notifications_lcd;
	int notifications_sound;
	int notifications_desktop;
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
void mcp_register(const char*, const int, const init_t, const stop_t, const loop_t);
