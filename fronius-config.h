// Fronius API is slow --> timings <5s make no sense
#define WAIT_OFFLINE		900
#define WAIT_STABLE			60
#define WAIT_IDLE			10
#define WAIT_NEXT			5

#define KEEP_FROM			25
#define KEEP_TO				75

#define HISTORY				24
#define OVERRIDE			600
#define STANDBY				0
#define STANDBY_RESET		60 * 30

#define MOSMIX				"/tmp/Rad1h-CHEMNITZ.txt"
//#define MOSMIX			"/tmp/Rad1h-MARIENBERG.txt"
//#define MOSMIX			"/tmp/Rad1h-BRAUNSDORF.txt"
#define MOSMIX_FACTOR		3

#define AKKU_CAPACITY		11000
#define SELF_CONSUMING		10000
#define BASELOAD			300
#define NOISE				20

#define URL_METER			"http://fronius/solar_api/v1/GetMeterRealtimeData.cgi?Scope=Device&DeviceId=0"
#define URL_FLOW10			"http://fronius10/solar_api/v1/GetPowerFlowRealtimeData.fcgi"
#define URL_FLOW7			"http://fronius7/solar_api/v1/GetPowerFlowRealtimeData.fcgi"

typedef struct _state state_t;

struct _state {
	int pv;
	int dpv;
	int grid;
	int akku;
	int surplus;
	int greedy;
	int modest;
	int steal;
	int waste;
	int sum;
	int chrg;
	int load;
	int dload;
	int pv10;
	int pv7;
	int distortion;
	int tendence;
	int wait;
};

typedef struct _device device_t;

typedef int (set_function_t)(device_t*, int);

struct _device {
	const char *name;
	const char *addr;
	const int adjustable;
	const int thermostat;
	const int load;
	int standby;
	int power;
	int dload;
	time_t override;
	set_function_t *set_function;
};

typedef struct potd_device_t {
	const int greedy;
	device_t *device;
} potd_device_t;

typedef struct potd_t {
	const char *name;
	const potd_device_t *devices[];
} potd_t;

// set device function signatures
int set_heater(device_t *device, int power);
int set_boiler(device_t *device, int power);

// devices
static device_t boiler1 = { .name = "boiler1", .load = 2000, .set_function = &set_boiler, .adjustable = 1, .thermostat = 1 };
static device_t boiler2 = { .name = "boiler2", .load = 2000, .set_function = &set_boiler, .adjustable = 1, .thermostat = 1 };
static device_t boiler3 = { .name = "boiler3", .load = 2000, .set_function = &set_boiler, .adjustable = 1, .thermostat = 1 };
static device_t plug9 = { .name = "plug9", .load = 700, .set_function = &set_heater, .adjustable = 0, .thermostat = 0 };
static device_t plug5 = { .name = "plug5", .load = 1000, .set_function = &set_heater, .adjustable = 0, .thermostat = 0 };
static device_t *devices[] = { &boiler1, &boiler2, &boiler3, &plug9, &plug5 };

// program of the day for cloudy weather with akku empty: priority is warm water in boiler1, then akku, then rest
static const potd_device_t CLOUDY_EMPTY_1 = { .device = &boiler1, .greedy = 1 };
static const potd_device_t CLOUDY_EMPTY_2 = { .device = &boiler2, .greedy = 0 };
static const potd_device_t CLOUDY_EMPTY_3 = { .device = &boiler3, .greedy = 0 };
static const potd_device_t CLOUDY_EMPTY_4 = { .device = &plug9, .greedy = 0 };
static const potd_t CLOUDY_EMPTY = { .name = "CLOUDY_EMPTY", .devices = { &CLOUDY_EMPTY_1, &CLOUDY_EMPTY_2, &CLOUDY_EMPTY_3, &CLOUDY_EMPTY_4, NULL } };

// program of the day for cloudy weather with akku full: priority is heater, then akku, then rest only when extra power
static const potd_device_t CLOUDY_FULL_1 = { .device = &plug9, .greedy = 1 };
static const potd_device_t CLOUDY_FULL_2 = { .device = &boiler1, .greedy = 0 };
static const potd_device_t CLOUDY_FULL_3 = { .device = &boiler2, .greedy = 0 };
static const potd_device_t CLOUDY_FULL_4 = { .device = &boiler3, .greedy = 0 };
static const potd_t CLOUDY_FULL = { .name = "CLOUDY_FULL", .devices = { &CLOUDY_FULL_1, &CLOUDY_FULL_2, &CLOUDY_FULL_3, &CLOUDY_FULL_4, NULL } };

// program of the day for sunny weather: plenty of power
static const potd_device_t SUNNY_1 = { .device = &plug9, .greedy = 1 };
static const potd_device_t SUNNY_5 = { .device = &plug5, .greedy = 1 };
static const potd_device_t SUNNY_2 = { .device = &boiler1, .greedy = 0 };
static const potd_device_t SUNNY_3 = { .device = &boiler2, .greedy = 0 };
static const potd_device_t SUNNY_4 = { .device = &boiler3, .greedy = 0 };
static const potd_t SUNNY = { .name = "SUNNY", .devices = { &SUNNY_1, &SUNNY_5, &SUNNY_2, &SUNNY_3, &SUNNY_4, NULL } };

// program of the day for cloudy weather but tomorrow sunny: steal all akku charge power
static const potd_device_t TOMORROW_1 = { .device = &plug9, .greedy = 1 };
static const potd_device_t TOMORROW_2 = { .device = &boiler1, .greedy = 1 };
static const potd_device_t TOMORROW_3 = { .device = &boiler2, .greedy = 1 };
static const potd_device_t TOMORROW_4 = { .device = &boiler3, .greedy = 1 };
static const potd_t TOMORROW = { .name = "TOMORROW", .devices = { &TOMORROW_1, &TOMORROW_2, &TOMORROW_3, &TOMORROW_4, NULL } };
