#define CHUNK_SIZE 			2048

// Fronius API is slow --> timings <5s make no sense
#define WAIT_OFFLINE		900
#define WAIT_STANDBY		300
#define WAIT_KEEP			60
#define WAIT_NEXT			5

#define STANDBY				20
#define STANDBY_EXPIRE		3600 / WAIT_STANDBY

#define KEEP_FROM			25
#define KEEP_TO				75

#define PV_HISTORY			24

#define URL_METER			"http://fronius/solar_api/v1/GetMeterRealtimeData.cgi?Scope=Device&DeviceId=0"
#define URL_FLOW			"http://fronius/solar_api/v1/GetPowerFlowRealtimeData.fcgi"

typedef struct get_response_t {
	char *buffer;
	size_t len;
	size_t buflen;
} get_response_t;

typedef int (*set_function_t)(void*, int);

typedef struct device_t {
	const char *name;
	const char *addr;
	int maximum;
	int adjustable;
	int greedy;
	int active;
	int override;
	int standby;
	int power;
	set_function_t set_function;
} device_t;

int set_heater(void *ptr, int power);
int set_boiler(void *ptr, int power);

// configuration for cloudy weather: priority is warm water in boiler1 and then akku
static device_t c1 = { .name = "boiler1", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1, .greedy = 1 };
static device_t c2 = { .name = "boiler2", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t c3 = { .name = "boiler3", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t c4 = { .name = "plug9", .maximum = 700, .set_function = &set_heater };
static device_t *CONFIG_CLOUDY[] = { &c1, &c2, &c3, &c4 };

// configuration for 50% sunny weather: priority is heater and then akku, boilers only from extra power
static device_t s501 = { .name = "plug9", .maximum = 700, .set_function = &set_heater, .greedy = 1 };
static device_t s502 = { .name = "boiler1", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t s503 = { .name = "boiler2", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t s504 = { .name = "boiler3", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t *CONFIG_SUNNY50[] = { &s501, &s502, &s503, &s504 };

// configuration for 100% sunny weather: akku will be full anyway at the end of day
static device_t s1001 = { .name = "plug9", .maximum = 700, .set_function = &set_heater, .greedy = 1 };
static device_t s1002 = { .name = "boiler1", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1, .greedy = 1 };
static device_t s1003 = { .name = "boiler2", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1, .greedy = 1 };
static device_t s1004 = { .name = "boiler3", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1, .greedy = 1 };
static device_t *CONFIG_SUNNY100[] = { &s1001, &s1002, &s1003, &s1004 };
