#define	CLIENT_ID		"tron-mcp"
#define	HOST			"mqtt"
#define PORT			"1883"

#define DBUS			"DISPLAY=:0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"
#define NOTIFY_SEND		"/usr/bin/notify-send -i /home/hje/Pictures/icons/mosquitto.png"
#define PLAY_MAU		"/usr/bin/aplay -q -D hw:CARD=Device_1 /home/hje/mau.wav"
#define MAC_HANDY		0xfc539ea93ac5
#define DARKNESS		50

#define NOTIFICATION	"notification"
#define SENSOR			"sensor"
#define TASMOTA			"tasmota"
#define NETWORK			"network"

typedef struct sensors_t {
	unsigned int bh1750_lux;
	float bmp085_temp;
	float bmp085_baro;
	float bmp280_temp;
	float bmp280_baro;
} sensors_t;

extern sensors_t *sensors;

int publish(const char*, const char*);
