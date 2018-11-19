#define ANUS
// #define PIWOLF
// #define SABRE18
// #define SABRE28

#ifdef ANUS
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/opt/music/"
#define DISPLAY			"/dev/tty"
#endif

#ifdef PIWOLF
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/public/music/"
#define WIRINGPI
#define LIRC_SEND
#define LIRC_RECEIVE
#endif

#ifdef SABRE18
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/public/music/"
//#define MUSIC 			"/music/"
#define DEVINPUT		"/dev/input/infrared"
#define WIRINGPI
#endif

#ifdef SABRE28
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/public/music/"
//#define MUSIC 			"/music/"
#define DEVINPUT		"/dev/input/infrared"
#define WIRINGPI
#define DISPLAY			"/dev/tty1"
//#define ROTARY
#define GPIO_ENC_A		4
#define GPIO_ENC_B		5
#define GPIO_SWITCH		6
#endif

#define EXTERNAL 		"/usr/local/bin/mcp-external.sh"
#define LIRC_REMOTE 	"audiophonics-wolfson"
#define LIRC_DEV 		"/run/lirc/lircd"
#define DEVINPUT		"/dev/input/infrared"
#define MPD_HOST		"localhost"
#define MPD_PORT		6600

#define msleep(x) usleep(x*1000)

#define BUFSIZE			256

#include <mpd/status.h>

typedef enum {
	startup, stdby, on, off
} power_state_t;

typedef enum {
	nlock, pcm, dsd, dop
} dac_signal_t;

typedef enum {
	i2s, opt, coax
} dac_source_t;

typedef struct {
	power_state_t power;
	dac_signal_t dac_signal;
	dac_source_t dac_source;
	int dac_bits;
	int dac_rate;
	int dac_volume;
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
} mcp_state_t;

extern mcp_state_t *mcp;

int startsWith(const char *pre, const char *str);
char *printBits(char value);

void mcplog(char *format, ...);

void *dac(void *arg);
int dac_init(void);
void dac_close(void);
void dac_on(void);
void dac_off(void);
void dac_mute(void);
void dac_unmute(void);
void dac_update(void);
void dac_volume_up(void);
void dac_volume_down(void);
void dac_handle(int key);

void *devinput(void *arg);
int devinput_init(void);
void devinput_close(void);
int devinput_find_key(const char *name);
char *devinput_keyname(unsigned int key);

void *lirc(void *arg);
int lirc_init(void);
void lirc_close(void);
void lirc_send(const char *remote, const char *command);

void *mpdclient(void *arg);
int mpdclient_init(void);
void mpdclient_close(void);
void mpdclient_set_playlist_mode(int mode);
void mpdclient_handle(int key);

void *rotary(void *arg);
int rotary_init(void);
void rotary_close(void);

int power_init(void);
void poweron(void);
void poweroff(void);
void standby(void);
void power_soft(void);
void power_hard(void);

void *display(void *arg);
int display_init(void);
void display_close(void);
void display_update(void);

void replaygain(const char *filename);
