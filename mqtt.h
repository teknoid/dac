#define	CLIENT_ID		"tron-mcp"
#define	HOST			"mqtt"
#define PORT			"1883"

#define APLAY_OPTIONS	"-q -D hw:CARD=Device"
#define APLAY_DIRECTORY "/home/hje/sounds/16"

#define DBUS			"DISPLAY=:0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"
#define NOTIFY_SEND		"/usr/bin/notify-send -i /home/hje/Pictures/icons/mosquitto.png"

#define MAC_HANDY		0xfc539ea93ac5
#define DARKNESS		50
#define	DOORBELL		0x670537

#define NOTIFICATION	"notification"
#define SENSOR			"sensor"
#define TASMOTA			"tasmota"
#define NETWORK			"network"

typedef struct sensors_t {
	unsigned int bh1750_lux;
	unsigned int bh1750_lux_mean;
	float bmp085_temp;
	float bmp085_baro;
	float bmp280_temp;
	float bmp280_baro;
} sensors_t;

extern sensors_t *sensors;

int publish(const char*, const char*);
