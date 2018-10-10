// #define ANUS
// #define PIWOLF
// #define SABRE
#define SABRE2

#define EXTERNAL 		"/usr/local/bin/mcp-external.sh"
#define LIRC_REMOTE 	"audiophonics-wolfson"
#define LIRC_DEV 		"/run/lirc/lircd"
#define MPD_HOST		"localhost"
#define MPD_PORT		6600

#ifdef ANUS
#define LOGFILE			"/var/log/mcp.log"
#define DEVINPUT		"/dev/input/infrared"
#define MUSIC 			"/opt/music/"
#endif

#ifdef PIWOLF
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/public/music/"
#define WIRINGPI
#define LIRC_SEND
#define LIRC_RECEIVE
#endif

#ifdef SABRE
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/public/music/"
//#define MUSIC 			"/music/"
#define DEVINPUT		"/dev/input/infrared"
#define WIRINGPI
#endif

#ifdef SABRE2
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/public/music/"
//#define MUSIC 			"/music/"
#define DEVINPUT		"/dev/input/infrared"
#define WIRINGPI
//#define ROTARY
#define GPIO_ENC_A		4
#define GPIO_ENC_B		5
#define GPIO_SWITCH		6
#endif

#define msleep(x) usleep(x*1000)

typedef enum {
	startup, stdby, on, off
} state_t;

struct plist {
	int key;
	int pos;
	char name[32];
	char path[128];
};

state_t power_state;

int startsWith(const char *pre, const char *str);

void mcplog(char *format, ...);

void *dac(void *arg);
int dac_init(void);
void dac_close(void);
void dac_on();
void dac_off();
void dac_update();
void dac_volume_up(void);
void dac_volume_down(void);
void dac_select_channel(void);
void dac_piwolf_channel(void);
void dac_piwolf_volume(void);

void *devinput(void *arg);
int devinput_init(void);
void devinput_close();
int find_key(char *name);
char *get_key_name(unsigned int key);

void *lirc(void *arg);
int lirc_init(void);
void lirc_close();
void lirc_send(const char *remote, const char *command);

void *mpdclient(void *arg);
int mpdclient_init(void);
void mpdclient_handle(int key);

void replaygain(const char *filename);

int power_init(void);
void poweron(void);
void poweroff(void);
void standby(void);
void power_soft(void);
void power_hard(void);

void *rotary(void *arg);
int rotary_init(void);
