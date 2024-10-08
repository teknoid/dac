#define GSTATE_HISTORY		14
#define PSTATE_HISTORY		6
#define OVERRIDE			600
#define STANDBY_RESET		60 * 30
#define STANDBY_NORESPONSE	3

// hexdump -v -e '5 "%10d " 13 "%8d ""\n"' /tmp/gstate.bin
// date --date='@1728165335'
#define GSTATE_FILE			"/tmp/gstate.bin"				// TODO later on hard disk for reboot!

#define AKKU_BURNOUT		1
#define AKKU_CAPACITY		11000
#define BASELOAD			300
#define SUSPICIOUS			250
#define NOISE				25

#ifdef FRONIUS_MAIN
#define TEMP_IN				22.0
#define TEMP_OUT			15.0
#else
#define TEMP_IN				sensors->htu21_temp
#define TEMP_OUT			sensors->sht31_temp
#endif

#define SUMMER				(4 < now->tm_mon && now->tm_mon < 8 && TEMP_OUT > 10 && TEMP_IN > 20) // April - September

#define ARRAY_SIZE(x) 		(sizeof(x) / sizeof(x[0]))

#define GREEDY_MODEST(d)	(d->greedy ? "greedy" : "modest")

#define AKKU_AVAILABLE		(AKKU_CAPACITY * (gstate->soc - 70) / 1000) // minus 7% minimum SoC
#define AKKU_CAPA_SOC(soc)	(AKKU_CAPACITY * soc / 1000)

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

typedef struct _pstate pstate_t;

struct _pstate {
	int pv;
	int dpv;
	int grid;
	int akku;
	int surplus;
	int greedy;
	int modest;
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

typedef struct _gstate gstate_t;

struct _gstate {
	int timestamp;
	int pv10;
	int pv7;
	int grid_produced;
	int grid_consumed;
	int pv10_24;
	int pv7_24;
	int grid_produced_24;
	int grid_consumed_24;
	int pv;
	int soc;
	int survive;
	int expected;
	int today;
	int tomorrow;
	int discharge;
	int ttl;
	int mosmix;
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
	int greedy;
	int noresponse;
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

// emergency: all power goes into akku
static const potd_t EMERGENCY = { .name = "EMERGENCY", .greedy = { 0 }, .modest = { 0 } };

// cloudy weather with akku empty: first charge akku, then boiler1, then rest
static const potd_t MODEST = { .name = "MODEST", .greedy = { 0 }, .modest = { &boiler1, &plug5, &plug6, &plug7, &plug8, &boiler2, &boiler3, 0 } };

// cloudy weather but tomorrow sunny: steal all akku charge power
static const potd_t GREEDY = { .name = "GREEDY", .greedy = { &plug5, &plug6, &plug7, &plug8, &boiler1, &boiler2, &boiler3, 0 }, .modest = { 0 } };

// sunny weather: plenty of power but modest boilers to catch all power when we have short sun spikes
static const potd_t SUNNY = { .name = "SUNNY", .greedy = { &plug5, &plug6, &plug7, &plug8, 0 }, .modest = { &boiler1, &boiler2, &boiler3, 0 } };
