#include "tasmota-devices.h"

// hexdump -v -e '4 "%10d ""\n"' /tmp/fronius-counter.bin
#define COUNTER_FILE			"/tmp/fronius-counter.bin"

// hexdump -v -e '15 "%6d ""\n"' /tmp/fronius-gstate.bin
#define GSTATE_FILE				"/tmp/fronius-gstate.bin"

// hexdump -v -e '24 "%6d ""\n"' /tmp/fronius-pstate*.bin
#define PSTATE_H_FILE			"/tmp/fronius-pstate-hours.bin"
#define PSTATE_M_FILE			"/tmp/fronius-pstate-minutes.bin"

#define AKKU_BURNOUT			1
#define MIN_SOC					70
#define BASELOAD				300
#define SUSPICIOUS				250
#define NOISE					25
#define OVERRIDE				600
#define STANDBY_NORESPONSE		5

#ifdef FRONIUS_MAIN
#define TEMP_IN					22.0
#define TEMP_OUT				15.0
#else
#define TEMP_IN					sensors->htu21_temp
#define TEMP_OUT				sensors->sht31_temp
#endif

#define SUMMER					(4 < now->tm_mon && now->tm_mon < 8 && TEMP_OUT > 10 && TEMP_IN > 20) 	// April - September
#define WINTER					(now->tm_mon == 11 || now->tm_mon == 12 || now->tm_mon == 1)			// November, Dezember, Januar

#define ARRAY_SIZE(x) 			(sizeof(x) / sizeof(x[0]))

#define GREEDY_MODEST(d)		(d->greedy ? "greedy" : "modest")

#define FLOAT10(x)				((float) x / 10.0)
#define FLOAT60(x)				((float) x / 60.0)

#define FLAG_DELTA				(1 << 0)
#define FLAG_RAMP				(1 << 1)
#define FLAG_STABLE				(1 << 2)
#define FLAG_DISTORTION			(1 << 3)
#define FLAG_CHECK_STANDBY		(1 << 4)
#define FLAG_EMERGENCY			(1 << 5)
#define FLAG_ALL_STANDBY		(1 << 6)
#define FLAG_ACTIVE				(1 << 7)
#define FLAG_BURNOUT			(1 << 14)
#define FLAG_OFFLINE			(1 << 15)

#define PSTATE_DELTA			(pstate->flags & FLAG_DELTA)
#define PSTATE_RAMP				(pstate->flags & FLAG_RAMP)
#define PSTATE_STABLE			(pstate->flags & FLAG_STABLE)
#define PSTATE_DISTORTION		(pstate->flags & FLAG_DISTORTION)
#define PSTATE_CHECK_STANDBY	(pstate->flags & FLAG_CHECK_STANDBY)
#define PSTATE_EMERGENCY		(pstate->flags & FLAG_EMERGENCY)
#define PSTATE_ALL_STANDBY		(pstate->flags & FLAG_ALL_STANDBY)
#define PSTATE_ACTIVE			(pstate->flags & FLAG_ACTIVE)
#define PSTATE_BURNOUT			(pstate->flags & FLAG_BURNOUT)
#define PSTATE_OFFLINE			(pstate->flags & FLAG_OFFLINE)

enum dstate {
	Disabled, Active, Standby, Standby_Check
};

typedef struct _counter counter_t;
struct _counter {
	int pv10;
	int pv7;
	int produced;
	int consumed;
};

typedef struct _gstate gstate_t;
#define GSTATE_SIZE		(sizeof(gstate_t) / sizeof(int))
#define GSTATE_HEADER	"    pv  pv10   pv7 ↑grid ↓grid today  tomo   sun   exp   soc  akku dakku   ttl  mosm  surv"
struct _gstate {
	int pv;
	int pv10;
	int pv7;
	int produced;
	int consumed;
	int today;
	int tomorrow;
	int sun;
	int expected;
	int soc;
	int akku;
	int dakku;
	int ttl;
	int mosmix;
	int survive;
};

typedef struct _pstate pstate_t;
#define PSTATE_SIZE		(sizeof(pstate_t) / sizeof(int))
#define PSTATE_HEADER	"    pv   Δpv   ∑pv  grid Δgrid ∑grid  akku  ac10   ac7  load Δload ∑load xload dxlod  dc10  10.1  10.2   dc7   7.1   7.2  grdy modst   soc flags"
struct _pstate {
	int pv;
	int dpv;
	int sdpv;
	int grid;
	int dgrid;
	int sdgrid;
	int akku;
	int ac10;
	int ac7;
	int load;
	int dload;
	int sdload;
	int xload;
	int dxload;
	int dc10;
	int pv10_1;
	int pv10_2;
	int dc7;
	int pv7_1;
	int pv7_2;
	int greedy;
	int modest;
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
	int aload;
	int xload;
	int greedy;
	int noresponse;
	int timer;
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
static device_t h3 = { .id = PLUG5, .r = 0, .name = "schlaf", .total = 500, .set_function = &set_heater, .adjustable = 0 };
static device_t h4 = { .id = PLUG6, .r = 0, .name = "tisch", .total = 200, .set_function = &set_heater, .adjustable = 0 };
static device_t *DEVICES[] = { &b1, &b2, &b3, &h1, &h2, &h3, &h4, 0 };

// program of the day
typedef struct potd_t {
	const char *name;
	device_t *greedy[ARRAY_SIZE(DEVICES)];
	device_t *modest[ARRAY_SIZE(DEVICES)];
} potd_t;

// charge: all power goes into akku, only boiler1 takes Fronius7 surplus power
static const potd_t CHARGE = { .name = "CHARGE", .greedy = { 0 }, .modest = { &b1, 0 } };

// cloudy weather with akku empty: first charge akku, then boiler1, then rest
static const potd_t MODEST = { .name = "MODEST", .greedy = { 0 }, .modest = { &b1, &h1, &h2, &h3, &h4, &b2, &b3, 0 } };

// cloudy weather but tomorrow sunny: steal all akku charge power
static const potd_t GREEDY = { .name = "GREEDY", .greedy = { &h1, &h2, &h3, &h4, &b1, &b2, &b3, 0 }, .modest = { 0 } };

// sunny weather: plenty of power but modest boilers to catch all power when we have short sun spikes
static const potd_t SUNNY = { .name = "SUNNY", .greedy = { &h1, &h2, &h3, &h4, 0 }, .modest = { &b1, &b2, &b3, 0 } };

// force boiler heating
static const potd_t WATER = { .name = "WATER", .greedy = { &b1, &b2, &b3, 0 }, .modest = { 0 } };
