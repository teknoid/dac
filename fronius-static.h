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

#define MOSMIX_FACTOR		3
#define AKKU_CAPACITY		11000
#define SELF_CONSUMING		10000

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

// program of the day for cloudy weather with akku empty: priority is warm water in boiler1, then akku, then rest
static device_t c11 = { .name = "boiler1", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1, .greedy = 1 };
static device_t c12 = { .name = "boiler2", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t c13 = { .name = "boiler3", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t c14 = { .name = "plug9", .maximum = 700, .set_function = &set_heater };
static device_t *POTD_CLOUDY_EMPTY[] = { &c11, &c12, &c13, &c14 };

// program of the day for cloudy weather with akku full: priority is heater, then akku, then rest only when extra power
static device_t c21 = { .name = "plug9", .maximum = 700, .set_function = &set_heater, .greedy = 1 };
static device_t c22 = { .name = "boiler1", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t c23 = { .name = "boiler2", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t c24 = { .name = "boiler3", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t *POTD_CLOUDY_FULL[] = { &c21, &c22, &c23, &c24 };

// program of the day for sunny weather: akku will be full anyway at the end of day
static device_t s1 = { .name = "plug9", .maximum = 700, .set_function = &set_heater, .greedy = 1 };
static device_t s2 = { .name = "boiler1", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1, .greedy = 1 };
static device_t s3 = { .name = "boiler2", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1, .greedy = 1 };
static device_t s4 = { .name = "boiler3", .maximum = 2000, .set_function = &set_boiler, .adjustable = 1, .greedy = 1 };
static device_t *POTD_SUNNY[] = { &s1, &s2, &s3, &s4 };
