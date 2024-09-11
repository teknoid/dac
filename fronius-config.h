#define KEEP_FROM			20
#define KEEP_TO				50

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

enum dstate {
	Standby, Active, Standby_Check, Request_Standby_Check
};

typedef struct _meter meter_t;

struct _meter {
	int p;
	int p1;
	int p2;
	int p3;
	int v1;
	int v2;
	int v3;
	int consumed;
	int produced;
};

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
	const unsigned int id;
	const unsigned int r;
	const char *name;
	const char *addr;
	const int adjustable;
	const int thermostat;
	const int load;
	enum dstate state;
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
static device_t plug5 = { .id = 0xB60A0C, .r = 0, .name = "plug5", .load = 150, .set_function = &set_heater, .adjustable = 0, .thermostat = 0 };
static device_t plug9 = { .id = 0x5EEEE8, .r = 0, .name = "plug9", .load = 450, .set_function = &set_heater, .adjustable = 0, .thermostat = 0 };
static device_t *devices[] = { &boiler1, &boiler2, &boiler3, &plug5, &plug9 };

// program of the day for cloudy weather with akku empty: priority is warm water in boiler1, then akku, then rest
static const potd_device_t CE1 = { .device = &boiler1, .greedy = 1 };
static const potd_device_t CE2 = { .device = &boiler2, .greedy = 0 };
static const potd_device_t CE3 = { .device = &boiler3, .greedy = 0 };
static const potd_device_t CE4 = { .device = &plug5, .greedy = 0 };
static const potd_device_t CE5 = { .device = &plug9, .greedy = 0 };
static const potd_t CLOUDY_EMPTY = { .name = "CLOUDY_EMPTY", .devices = { &CE1, &CE2, &CE3, &CE4, &CE5, NULL } };

// program of the day for cloudy weather with akku full: priority is heater, then akku, then rest only when extra power
static const potd_device_t CF1 = { .device = &plug5, .greedy = 1 };
static const potd_device_t CF2 = { .device = &plug9, .greedy = 1 };
static const potd_device_t CF3 = { .device = &boiler1, .greedy = 1 };
static const potd_device_t CF4 = { .device = &boiler2, .greedy = 0 };
static const potd_device_t CF5 = { .device = &boiler3, .greedy = 0 };
static const potd_t CLOUDY_FULL = { .name = "CLOUDY_FULL", .devices = { &CF1, &CF2, &CF3, &CF4, &CF5, NULL } };

// program of the day for sunny weather: plenty of power
static const potd_device_t S1 = { .device = &plug5, .greedy = 1 };
static const potd_device_t S2 = { .device = &plug9, .greedy = 1 };
static const potd_device_t S3 = { .device = &boiler1, .greedy = 1 };
static const potd_device_t S4 = { .device = &boiler2, .greedy = 0 };
static const potd_device_t S5 = { .device = &boiler3, .greedy = 0 };
static const potd_t SUNNY = { .name = "SUNNY", .devices = { &S1, &S2, &S3, &S4, &S5, NULL } };

// program of the day for cloudy weather but tomorrow sunny: steal all akku charge power
static const potd_device_t T1 = { .device = &plug5, .greedy = 1 };
static const potd_device_t T2 = { .device = &plug9, .greedy = 1 };
static const potd_device_t T3 = { .device = &boiler1, .greedy = 1 };
static const potd_device_t T4 = { .device = &boiler2, .greedy = 1 };
static const potd_device_t T5 = { .device = &boiler3, .greedy = 1 };
static const potd_t TOMORROW = { .name = "TOMORROW", .devices = { &T1, &T2, &T3, &T4, &T5, NULL } };
