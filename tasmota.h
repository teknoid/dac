#define SENSORS_JSON		"/run/mcp/sensors.json"

typedef struct tasmota_config_t {
	const unsigned int id;
	const unsigned int relay;
	const unsigned int t1;
	const unsigned int t1b;
	const unsigned int t2;
	const unsigned int t2b;
	const unsigned int t3;
	const unsigned int t3b;
	const unsigned int t4;
	const unsigned int t4b;
	const unsigned int timer;
} tasmota_config_t;

typedef struct tasmota_state_t {
	unsigned int id;
	unsigned int relay1;
	unsigned int relay2;
	unsigned int relay3;
	unsigned int relay4;
	unsigned int position;
	unsigned int timer;
	void *next;
} tasmota_state_t;

int openbeken_color(unsigned int id, int r, int g, int b);
int openbeken_dimmer(unsigned int id, int d);
int openbeken_set(unsigned int id, int channel, int value);

int tasmota_power(unsigned int id, int relay, int power);
int tasmota_power_on(unsigned int id);
int tasmota_power_off(unsigned int id);
int tasmota_power_get(unsigned int id, int relay);

int tasmota_shutter(unsigned int, unsigned int);

int tasmota_dispatch(const char *topic, uint16_t tsize, const char *message, size_t msize);
