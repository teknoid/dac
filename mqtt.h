#define	CLIENT_ID		"tron-mcp"
#define	HOST			"mqtt"
#define PORT			"1883"

#define NOTIFICATION	"notification"

#define SENSOR			"sensors/7A1F60"

typedef struct sensors_t {
	unsigned int bh1750_lux;
	float bmp085_temp;
	float bmp085_baro;
	float bmp280_temp;
	float bmp280_baro;
} sensors_t;

extern sensors_t *sensors;

int publish(const char *topic, const char *message);
