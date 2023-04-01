#define	CLIENT_ID		"tron-mcp"
#define	HOST			"mqtt"
#define PORT			"1883"

#define DBUS			"DISPLAY=:0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"
#define NOTIFYSEND		"/usr/bin/notify-send -i /home/hje/Pictures/icons/mosquitto.png"

#define NOTIFICATION	"notification"
#define SENSOR			"sensor"
#define SHELLY			"shelly"
#define DNSMASQ			"dnsmasq"

typedef struct sensors_t {
	unsigned int bh1750_lux;
	float bmp085_temp;
	float bmp085_baro;
	float bmp280_temp;
	float bmp280_baro;
} sensors_t;

extern sensors_t *sensors;

int publish(const char*, const char*);
