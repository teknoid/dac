#define TOPIC_NOTIFICATION	"notification"
#define TOPIC_TASMOTA		"tasmota"
#define TOPIC_SENSOR		"sensor"
#define TOPIC_SOLAR			"solar"
#define TOPIC_NETWORK		"network"
#define TOPIC_TELE			"tele"
#define TOPIC_CMND			"cmnd"
#define TOPIC_STAT			"stat"

int notify(const char *title, const char *text, const char *sound);
int notify_red(const char *title, const char *text, const char *sound);

int publish(const char *topic, const char *message, int retain);
int publish_oneshot(const char *topic, const char *message, int retain);
