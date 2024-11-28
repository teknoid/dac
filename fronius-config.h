#include "tasmota-devices.h"

#define COUNTER_HISTORY			30		// days
#define GSTATE_HISTORY			24		// hours
#define PSTATE_HISTORY			32		// samples
#define OVERRIDE				600
#define STANDBY_RESET			60 * 30
#define STANDBY_NORESPONSE		5

// date --date='@1728165335'

// hexdump -v -e '5 "%10d ""\n"' /work/fronius-counter.bin
#define COUNTER_FILE			"/work/fronius-counter.bin"

// hexdump -v -e '16 "%6d ""\n"' /work/fronius-gstate.bin
#define GSTATE_FILE				"/work/fronius-gstate.bin"

// hexdump -v -e '1 "%10d " 3 "%8d ""\n"' /work/fronius-minmax.bin
#define MINMAX_FILE				"/work/fronius-minmax.bin"

#define AKKU_BURNOUT			1
#define AKKU_CAPACITY			11000
#define BASELOAD				300
#define SUSPICIOUS				250
#define NOISE					25
#define EMERGENCY				(AKKU_CAPACITY / 10)
#define PV_MIN					100

#ifdef FRONIUS_MAIN
#define TEMP_IN					22.0
#define TEMP_OUT				15.0
#else
#define TEMP_IN					sensors->htu21_temp
#define TEMP_OUT				sensors->sht31_temp
#endif

#define SUMMER					(4 < now->tm_mon && now->tm_mon < 8 && TEMP_OUT > 10 && TEMP_IN > 20) // April - September

#define ARRAY_SIZE(x) 			(sizeof(x) / sizeof(x[0]))

#define GREEDY_MODEST(d)		(d->greedy ? "greedy" : "modest")

#define AKKU_CAPACITY_SOC(soc)	(AKKU_CAPACITY * soc / 1000)

#define FLOAT10(x)				((float) x / 10.0)
#define FLOAT60(x)				((float) x / 60.0)

#define FLAG_VALID				(1 << 0)
#define FLAG_STABLE				(1 << 1)
#define FLAG_DISTORTION			(1 << 2)
#define FLAG_CHECK_STANDBY		(1 << 3)
#define FLAG_EMERGENCY			(1 << 4)
#define FLAG_BURNOUT			(1 << 5)
#define FLAG_ALL_STANDBY		(1 << 6)
#define FLAG_ALL_OFF			(1 << 7)
#define FLAG_OFFLINE			(1 << 15)

#define PSTATE_VALID			(pstate->flags & FLAG_VALID)
#define PSTATE_STABLE			(pstate->flags & FLAG_STABLE)
#define PSTATE_DISTORTION		(pstate->flags & FLAG_DISTORTION)
#define PSTATE_CHECK_STANDBY	(pstate->flags & FLAG_CHECK_STANDBY)
#define PSTATE_EMERGENCY		(pstate->flags & FLAG_EMERGENCY)
#define PSTATE_BURNOUT			(pstate->flags & FLAG_BURNOUT)
#define PSTATE_ALL_STANDBY		(pstate->flags & FLAG_ALL_STANDBY)
#define PSTATE_ALL_OFF			(pstate->flags & FLAG_ALL_OFF)
#define PSTATE_OFFLINE			(pstate->flags & FLAG_OFFLINE)

enum dstate {
	Disabled, Active, Standby, Standby_Check
};

typedef struct _raw raw_t;

struct _raw {
	float akku;
	float grid;
	float load;
	float pv10;
	float pv10_total1;
	float pv10_total2;
	float pv7;
	float pv7_total;
	float soc;
	float produced;
	float consumed;
	float p;
	float v1;
	float v2;
	float v3;
	float f;
};

typedef struct _counter counter_t;

struct _counter {
	int timestamp;
	int pv10;
	int pv7;
	int produced;
	int consumed;
};

typedef struct _gstate gstate_t;

struct _gstate {
	int timestamp;
	int pv;
	int pv10;
	int pv7;
	int produced;
	int consumed;
	int today;
	int tomorrow;
	int baseload;
	int expected;
	int akku;
	int soc;
	int ttl;
	int noon;
	int mosmix;
	int survive;
};

typedef struct _pstate pstate_t;

struct _pstate {
	int pv;
	int dpv;
	int grid;
	int dgrid;
	int akku;
	int ac10;
	int ac7;
	int load;
	int dload;
	int xload;
	int dxload;
	int dc10;
	int pv10_1;
	int pv10_2;
	int dc7;
	int pv7_1;
	int pv7_2;
	int surplus;
	int greedy;
	int modest;
	int tendence;
	int wait;
	int soc;
	int flags;
};

typedef struct _minmax minmax_t;

struct _minmax {
	int v1min_ts;
	int v1min;
	int v12min;
	int v13min;
	int v2min_ts;
	int v21min;
	int v2min;
	int v23min;
	int v3min_ts;
	int v31min;
	int v32min;
	int v3min;
	int v1max_ts;
	int v1max;
	int v12max;
	int v13max;
	int v2max_ts;
	int v21max;
	int v2max;
	int v23max;
	int v3max_ts;
	int v31max;
	int v32max;
	int v3max;
	int fmin_ts;
	int fmin;
	int fmax_ts;
	int fmax;
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
static device_t b1 = { .name = "boiler1", .total = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t b2 = { .name = "boiler2", .total = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t b3 = { .name = "boiler3", .total = 2000, .set_function = &set_boiler, .adjustable = 1 };
static device_t h1 = { .id = SWITCHBOX, .r = 1, .name = "küche", .total = 500, .set_function = &set_heater, .adjustable = 0 };
static device_t h2 = { .id = SWITCHBOX, .r = 2, .name = "wozi", .total = 500, .set_function = &set_heater, .adjustable = 0 };
static device_t h3 = { .id = PLUG5, .r = 3, .name = "schlaf", .total = 500, .set_function = &set_heater, .adjustable = 0 };
static device_t h4 = { .id = PLUG6, .r = 0, .name = "tisch", .total = 200, .set_function = &set_heater, .adjustable = 0 };
static device_t *DEVICES[] = { &b1, &b2, &b3, &h1, &h2, &h3, &h4, 0 };

// program of the day
typedef struct potd_t {
	const char *name;
	device_t *greedy[ARRAY_SIZE(DEVICES)];
	device_t *modest[ARRAY_SIZE(DEVICES)];
} potd_t;

// charge: all power goes into akku
static const potd_t CHARGE = { .name = "CHARGE", .greedy = { 0 }, .modest = { 0 } };

// cloudy weather with akku empty: first charge akku, then boiler1, then rest
static const potd_t MODEST = { .name = "MODEST", .greedy = { 0 }, .modest = { &b1, &h1, &h2, &h3, &h4, &b2, &b3, 0 } };

// cloudy weather but tomorrow sunny: steal all akku charge power
static const potd_t GREEDY = { .name = "GREEDY", .greedy = { &h1, &h2, &h3, &h4, &b1, &b2, &b3, 0 }, .modest = { 0 } };

// sunny weather: plenty of power but modest boilers to catch all power when we have short sun spikes
static const potd_t SUNNY = { .name = "SUNNY", .greedy = { &h1, &h2, &h3, &h4, 0 }, .modest = { &b1, &b2, &b3, 0 } };
