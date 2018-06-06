#define ANUS
// #define PIWOLF
// #define SABRE

#define EXTERNAL 		"/usr/local/bin/mcp-external.sh"
#define LIRC_REMOTE 	"audiophonics-wolfson"
#define LIRC_DEV 		"/run/lirc/lircd"
#define MPD_HOST		"localhost"
#define MPD_PORT		6600

#ifdef ANUS
#define LOGFILE			"/var/log/mcp.log"
#define DEVINPUT		"/dev/input/infrared"
#define MUSIC 			"/media/hje/349ece80-f9a0-47d1-bd95-8d02fb097650/music/"
#endif

#ifdef PIWOLF
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/public/music/"
#define WIRINGPI
#define GPIO_POWER		0
#define LIRC_SEND
#define LIRC_RECEIVE
#endif

#ifdef SABRE
#define LOGFILE			"/var/log/mcp.log"
#define MUSIC 			"/public/music/"
#define DEVINPUT		"/dev/input/infrared"
#define WIRINGPI
#define GPIO_POWER		5
#define GPIO_VOL_UP		4
#define GPIO_VOL_DOWN	0
#endif

#define msleep(x) usleep(x*1000)

struct plist {
	int key;
	int pos;
	char name[32];
	char path[128];
};

int startsWith(const char *pre, const char *str);

void mcplog(char *format, ...);

void* dac(void *arg);
int dac_init(void);
void dac_volume_up(void);
void dac_volume_down(void);
void dac_select_channel(void);
void dac_piwolf_channel(void);
void dac_piwolf_volume(void);

void* devinput(void *arg);
int devinput_init(void);
int find_key(char *name);
char *get_key_name(unsigned int key);

void* lirc(void *arg);
int lirc_init(void);
void lirc_send(const char *remote, const char *command);

void* mpdclient(void *arg);
int mpdclient_init(void);
void mpdclient_handle(int key);

void replaygain(const char *filename);

int power_init(void);
void poweron(void);
void poweroff(void);
void standby(void);
void power_soft(void);
void power_hard(void);

void* rotary(void *arg);
int rotary_init(void);
