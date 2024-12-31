#include "tasmota-devices.h"

// hexdump -v -e '6 "%10d ""\n"' /tmp/fronius-counter.bin
#define COUNTER_FILE			"/tmp/fronius-counter.bin"

// hexdump -v -e '16 "%6d ""\n"' /tmp/fronius-gstate.bin
#define GSTATE_FILE				"/tmp/fronius-gstate.bin"

// hexdump -v -e '23 "%6d ""\n"' /tmp/fronius-pstate*.bin
#define PSTATE_H_FILE			"/tmp/fronius-pstate-hours.bin"
#define PSTATE_M_FILE			"/tmp/fronius-pstate-minutes.bin"

#define AKKU_BURNOUT			1
#define BASELOAD				300
#define SUSPICIOUS				250
#define NOISE					25
#define OVERRIDE				600
#define STANDBY_NORESPONSE		5

// grid +/-25 around 25 --> stable from 0..50
#define RAMP_WINDOW				25
#define RAMP_OFFSET				25

#ifdef FRONIUS_MAIN
#define TEMP_IN					22.0
#define TEMP_OUT				15.0
#else
#define TEMP_IN					sensors->htu21_temp
#define TEMP_OUT				sensors->sht31_temp
#endif

#define SUMMER					(4 < now->tm_mon && now->tm_mon < 8 && TEMP_OUT > 10 && TEMP_IN > 20) 	// April - September
#define WINTER					(now->tm_mon == 10 || now->tm_mon == 11 || now->tm_mon == 0)			// November, Dezember, Januar

#define ARRAY_SIZE(x) 			(sizeof(x) / sizeof(x[0]))

#define GREEDY_MODEST(d)		(d->greedy ? "greedy" : "modest")

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
#define COUNTER_SIZE		(sizeof(counter_t) / sizeof(int))
#define COUNTER_HEADER	" ↑grid ↓grid mppt1 mppt2 mppt3 mppt4"
struct _counter {
	int produced;
	int consumed;
	int mppt1;
	int mppt2;
	int mppt3;
	int mppt4;
};

typedef struct _gstate gstate_t;
#define GSTATE_SIZE		(sizeof(gstate_t) / sizeof(int))
#define GSTATE_HEADER	"    pv ↑grid ↓grid today  tomo   exp mppt1 mppt2 mppt3 mppt4   soc  akku Δakku   ttl  surv  heat"
struct _gstate {
	int pv;
	int produced;
	int consumed;
	int today;
	int tomorrow;
	int expected;
	int mppt1;
	int mppt2;
	int mppt3;
	int mppt4;
	int soc;
	int akku;
	int dakku;
	int ttl;
	int survive;
	int heating;
};

typedef struct _pstate pstate_t;
#define PSTATE_SIZE		(sizeof(pstate_t) / sizeof(int))
#define PSTATE_HEADER	"    pv   Δpv   ∑pv  grid Δgrid ∑grid  akku  ac10   ac7  load Δload ∑load xload dxlod  dc10   dc7 mppt1 mppt2 mppt3 mppt4  ramp   soc flags"
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
	int dc7;
	int mppt1;
	int mppt2;
	int mppt3;
	int mppt4;
	int ramp;
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
typedef int (ramp_function_t)(device_t*, int);
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
	int noresponse;
	int timer;
	time_t override;
	ramp_function_t *ramp_function;
};

// set device function signatures
int ramp_heater(device_t *device, int power);
int ramp_boiler(device_t *device, int power);
int ramp_akku(device_t *device, int power);

// devices
static device_t a1 = { .name = "akku", .total = 0, .ramp_function = &ramp_akku, .adjustable = 0 };
static device_t b1 = { .name = "boiler1", .total = 2000, .ramp_function = &ramp_boiler, .adjustable = 1 };
static device_t b2 = { .name = "boiler2", .total = 2000, .ramp_function = &ramp_boiler, .adjustable = 1 };
static device_t b3 = { .name = "boiler3", .total = 2000, .ramp_function = &ramp_boiler, .adjustable = 1 };
static device_t h1 = { .id = SWITCHBOX, .r = 1, .name = "küche", .total = 500, .ramp_function = &ramp_heater, .adjustable = 0 };
static device_t h2 = { .id = SWITCHBOX, .r = 2, .name = "wozi", .total = 500, .ramp_function = &ramp_heater, .adjustable = 0 };
static device_t h3 = { .id = PLUG5, .r = 0, .name = "schlaf", .total = 500, .ramp_function = &ramp_heater, .adjustable = 0 };
static device_t h4 = { .id = PLUG6, .r = 0, .name = "tisch", .total = 200, .ramp_function = &ramp_heater, .adjustable = 0 };
static device_t *DEVICES[] = { &a1, &b1, &b2, &b3, &h1, &h2, &h3, &h4, 0 };

// program of the day
typedef struct potd_t {
	const char *name;
	device_t *devices[ARRAY_SIZE(DEVICES) + 1];
} potd_t;

// first charge akku, then boilers, then heaters
static const potd_t MODEST = { .name = "MODEST", .devices = { &a1, &b1, &h1, &h2, &h3, &h4, &b2, &b3, 0 } };

// steal all akku charge power
static const potd_t GREEDY = { .name = "GREEDY", .devices = { &h1, &h2, &h3, &h4, &b1, &b2, &b3, &a1, 0 } };

// heaters, then akku, then boilers (catch remaining pv from secondary inverters or if akku is not able to consume all generated power)
static const potd_t PLENTY = { .name = "PLENTY", .devices = { &h1, &h2, &h3, &h4, &a1, &b1, &b2, &b3, 0 } };

// force boiler heating first
static const potd_t BOILER1 = { .name = "BOILER1", .devices = { &b1, &a1, &b2, &b3, &h1, &h2, &h3, &h4, 0 } };
static const potd_t BOILER3 = { .name = "BOILER3", .devices = { &b3, &a1, &b2, &b1, &h1, &h2, &h3, &h4, 0 } };
