#define HISTORY				24
#define OVERRIDE			600
#define STANDBY				0
#define STANDBY_RESET		60 * 30
#define STANDBY_COUNTER		3

#define MOSMIX				"/tmp/Rad1h-CHEMNITZ.txt"
//#define MOSMIX			"/tmp/Rad1h-MARIENBERG.txt"
//#define MOSMIX			"/tmp/Rad1h-BRAUNSDORF.txt"
#define MOSMIX_FACTOR		3

#define AKKU_BURNOUT		1
#define AKKU_CAPACITY		11000
#define BASELOAD			300
#define HEATING				2000	 			// heating 2kw
#define NOISE				25
#define SUSPICIOUS			250

#define TEMP_IN				sensors->htu21_temp
#define TEMP_OUT			sensors->sht31_temp
#define SUMMER				(4 < now->tm_mon && now->tm_mon < 8 && TEMP_OUT > 10 && TEMP_IN > 20) // April - September

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

enum dstate {
	Disabled, Active, Standby, Standby_Check
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
	int waste;
	int sum;
	int soc;
	int load;
	int dload;
	int xload;
	int dxload;
	int cload;
	int pv10;
	int pv7;
	int distortion;
	int tendence;
	int standby;
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
	const int total;
	enum dstate state;
	int power;
	int load;
	int dload;
	int standby_counter;
	time_t override;
	set_function_t *set_function;
};

// set device function signatures
int set_heater(device_t *device, int power);
int set_boiler(device_t *device, int power);

// devices
static device_t boiler1 = { .name = "boiler1", .total = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t boiler2 = { .name = "boiler2", .total = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t boiler3 = { .name = "boiler3", .total = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t plug5 = { .id = 0xB60A0C, .r = 0, .name = "plug5", .total = 500, .set_function = &set_heater, .adjustable = 0 };
static device_t plug6 = { .id = 0x5E40EC, .r = 0, .name = "plug6", .total = 500, .set_function = &set_heater, .adjustable = 0 };
static device_t plug7 = { .id = 0xC24A88, .r = 0, .name = "plug7", .total = 500, .set_function = &set_heater, .adjustable = 0 };
static device_t plug8 = { .id = 0x58ED80, .r = 0, .name = "plug8", .total = 200, .set_function = &set_heater, .adjustable = 0 };
static device_t *DEVICES[] = { &boiler1, &boiler2, &boiler3, &plug5, &plug6, &plug7, &plug8, 0 };

// program of the day
typedef struct potd_t {
	const char *name;
	device_t *greedy[ARRAY_SIZE(DEVICES)];
	device_t *modest[ARRAY_SIZE(DEVICES)];
} potd_t;

// cloudy weather with akku empty: priority is warm water in boiler1, then akku, then rest
static const potd_t EMPTY = { .name = "EMPTY", .greedy = { &boiler1, 0 }, .modest = { &plug5, &plug6, &plug7, &plug8, &boiler2, &boiler3, 0 } };

// cloudy weather but tomorrow sunny: steal all akku charge power
static const potd_t TOMORROW = { .name = "TOMORROW", .greedy = { &boiler1, &plug5, &plug6, &plug7, &plug8, &boiler2, &boiler3, 0 }, .modest = { 0 } };

// sunny weather: plenty of power
static const potd_t SUNNY = { .name = "SUNNY", .greedy = { &boiler1, &plug5, &plug6, &plug7, &plug8, 0 }, .modest = { &boiler2, &boiler3, 0 } };
