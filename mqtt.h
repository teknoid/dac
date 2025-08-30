#define	HOST				"mqtt"
#define PORT				"1883"

#define APLAY_OPTIONS		"-q -D hw:CARD=Device"
#define APLAY_DIRECTORY 	"/home/hje/sounds/16"

#define DBUS				"DISPLAY=:0 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus"
#define NOTIFY_SEND			"/usr/bin/notify-send -i /home/hje/Pictures/icons/mosquitto.png"

#define MAC_HANDY			0xfc539ea93ac5
#define DARKNESS			50

#define TOPIC_NOTIFICATION	"notification"
#define TOPIC_SENSOR		"sensor"
#define TOPIC_NETWORK		"network"
#define TOPIC_TELE			"tele"
#define TOPIC_CMND			"cmnd"
#define TOPIC_STAT			"stat"

int notify(const char *title, const char *text, const char *sound);
int notify_red(const char *title, const char *text, const char *sound);

int publish(const char *topic, const char *message, int retain);
