// #define ANUS
// #define PIWOLF
// #define SABRE18
#define SABRE28

#ifdef ANUS
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/opt/music/"
#define DISPLAY			"/dev/tty"
#endif

#ifdef PIWOLF
#define WIRINGPI
#define LIRC_SEND
#define LIRC_RECEIVE
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/public/music/"
#endif

#ifdef SABRE18
#define WIRINGPI
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/public/music/"
//#define MUSIC 			"/music/"
#define DEVINPUT_IR		"/dev/input/infrared"
#endif

#ifdef SABRE28
#define WIRINGPI
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/public/music/"
#define DEVINPUT_IR		"/dev/input/infrared"
#define DEVINPUT_RA		"/dev/input/rotary_axis"
#define DEVINPUT_RB		"/dev/input/rotary_button"
#define DISPLAY			"/dev/tty1"
#define I2C				"/dev/i2c-0"
#endif

#define EXTERNAL 		"/usr/local/bin/mcp-external.sh"
#define LIRC_REMOTE 	"audiophonics-wolfson"
#define LIRC_DEV 		"/run/lirc/lircd"
#define MPD_HOST		"localhost"
#define MPD_PORT		6600

#define BUFSIZE			256

#include <mpd/status.h>

typedef enum {
	nlock, pcm, dsd, dop
} dac_signal_t;

typedef enum {
	i2s, opt, coax
} dac_source_t;

typedef struct mcp_state_t {
	dac_signal_t dac_signal;
	dac_source_t dac_source;
	int ext_power;
	int dac_power;
	int dac_bits;
	int dac_rate;
	int dac_volume;
	int dac_mute;
	enum mpd_state mpd_state;
	int mpd_bits;
	int mpd_rate;
	int clock_h;
	int clock_m;
	int clock_tick;
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
} mcp_state_t;
extern mcp_state_t *mcp;

typedef struct mcp_config_t {
	int daemonize;
} mcp_config_t;
extern mcp_config_t *cfg;

int dac_init(void);
void dac_close(void);
void dac_power();
void dac_mute(void);
void dac_unmute(void);
void dac_update(void);
void dac_volume_up(void);
void dac_volume_down(void);
void dac_handle(int c);

int ir_init(void);
void ir_close(void);

int rotary_init(void);
void rotary_close(void);

int lirc_init(void);
void lirc_close(void);
void lirc_send(const char *remote, const char *command);

int mpdclient_init(void);
void mpdclient_close(void);
void mpdclient_handle(int key);

void replaygain(const char *filename);

void system_shutdown(void);
