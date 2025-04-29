#include "tasmota-devices.h"

// hexdump -v -e '6 "%10d ""\n"' /work/fronius-counter.bin
#define COUNTER_FILE			"/work/fronius-counter.bin"

// hexdump -v -e '20 "%6d ""\n"' /work/fronius-gstate.bin
#define GSTATE_FILE				"/work/fronius-gstate.bin"
#define GSTATE_TODAY_CSV		"/run/mcp/gstate-today.csv"
#define GSTATE_WEEK_CSV			"/run/mcp/gstate-week.csv"

// hexdump -v -e '33 "%6d ""\n"' /work/fronius-pstate*.bin
#define PSTATE_H_FILE			"/work/fronius-pstate-hours.bin"
#define PSTATE_M_FILE			"/work/fronius-pstate-minutes.bin"
#define PSTATE_M_CSV			"/run/mcp/pstate-minutes.csv"
#define PSTATE_JSON				"/run/mcp/pstate.json"
#define GSTATE_JSON				"/run/mcp/gstate.json"
#define DSTATE_JSON				"/run/mcp/dstate.json"
#define POWERFLOW_JSON			"/run/mcp/powerflow.json"

#define DSTATE_TEMPLATE			"{\"name\":\"%s\", \"state\":%d, \"power\":%d, \"total\":%d, \"load\":%d}"
#define POWERFLOW_TEMPLATE		"{\"common\":{\"datestamp\":\"01.01.2025\",\"timestamp\":\"00:00:00\"},\"inverters\":[{\"BatMode\":1,\"CID\":0,\"DT\":0,\"E_Total\":1,\"ID\":1,\"P\":1,\"SOC\":%f}],\"site\":{\"BackupMode\":false,\"BatteryStandby\":false,\"E_Day\":null,\"E_Total\":1,\"E_Year\":null,\"MLoc\":0,\"Mode\":\"bidirectional\",\"P_Akku\":%d,\"P_Grid\":%d,\"P_Load\":%d,\"P_PV\":%d,\"rel_Autonomy\":100.0,\"rel_SelfConsumption\":100.0},\"version\":\"13\"}"

#define SUMMER					(4 <= now->tm_mon && now->tm_mon <= 8) 									// May - September
#define WINTER					(now->tm_mon == 10 || now->tm_mon == 11 || now->tm_mon == 0)			// November, Dezember, Januar

#define AKKU_BURNOUT			1
#define SUSPICIOUS				500
#define BASELOAD				(WINTER ? 300 : 200)
#define MINIMUM					(BASELOAD / 2)
#define RAMP_WINDOW				35
#define NOISE					10
#define OVERRIDE				600
#define STANDBY_NORESPONSE		5

#ifdef FRONIUS_MAIN
#define TEMP_IN					22.0
#define TEMP_OUT				15.0
#else
#define TEMP_IN					sensors->htu21_temp
#define TEMP_OUT				sensors->sht31_temp
#endif

#define ARRAY_SIZE(x) 			(sizeof(x) / sizeof(x[0]))

#define GREEDY_MODEST(d)		(d->greedy ? "greedy" : "modest")

#define FLAG_DELTA				(1 << 0)
#define FLAG_VALID				(1 << 1)
#define FLAG_STABLE				(1 << 2)
#define FLAG_DISTORTION			(1 << 3)
#define FLAG_CHECK_STANDBY		(1 << 4)
#define FLAG_EMERGENCY			(1 << 5)
#define FLAG_ALL_STANDBY		(1 << 6)
#define FLAG_ACTIVE				(1 << 7)
#define FLAG_BURNOUT			(1 << 14)
#define FLAG_OFFLINE			(1 << 15)

#define PSTATE_DELTA			(pstate->flags & FLAG_DELTA)
#define PSTATE_VALID			(pstate->flags & FLAG_VALID)
#define PSTATE_STABLE			(pstate->flags & FLAG_STABLE)
#define PSTATE_DISTORTION		(pstate->flags & FLAG_DISTORTION)
#define PSTATE_CHECK_STANDBY	(pstate->flags & FLAG_CHECK_STANDBY)
#define PSTATE_EMERGENCY		(pstate->flags & FLAG_EMERGENCY)
#define PSTATE_ALL_STANDBY		(pstate->flags & FLAG_ALL_STANDBY)
#define PSTATE_ACTIVE			(pstate->flags & FLAG_ACTIVE)
#define PSTATE_BURNOUT			(pstate->flags & FLAG_BURNOUT)
#define PSTATE_OFFLINE			(pstate->flags & FLAG_OFFLINE)

#define DD						(*dd)
#define UP						(*dd)->total
#define DOWN					(*dd)->total * -1

enum dstate {
	Disabled, Active, Active_Checked, Standby, Standby_Check, Charge, Discharge
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
#define GSTATE_HEADER	"    pv ↑grid ↓grid today  tomo   sod   eod nsurv nheat mppt1 mppt2 mppt3 mppt4   soc  akku Δakku   ttl  succ  surv  heat"
struct _gstate {
	int pv;
	int produced;
	int consumed;
	int today;
	int tomorrow;
	int sod;
	int eod;
	int need_survive;
	int need_heating;
	int mppt1;
	int mppt2;
	int mppt3;
	int mppt4;
	int soc;
	int akku;
	int dakku;
	int ttl;
	int success;
	int survive;
	int heating;
};

// needed for migration
typedef struct gstate_old_t {
	int pv;
	int produced;
	int consumed;
	int today;
	int tomorrow;
	int sod;
	int eod;
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
	int success;
} gstate_old_t;

typedef struct _pstate pstate_t;
#define PSTATE_SIZE		(sizeof(pstate_t) / sizeof(int))
#define PSTATE_HEADER	"    pv   Δpv   ∑pv pvmin pvmax  grid Δgrid ∑grid  akku  ac10   ac7  load Δload ∑load xload dxlod  dc10   dc7 mppt1 mppt2 mppt3 mppt4    p1    p2    p3    v1    v2    v3     f  ramp   soc timer flags"
struct _pstate {
	int pv;
	int dpv;
	int sdpv;
	int pvmin;
	int pvmax;
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
	int p1;
	int p2;
	int p3;
	int v1;
	int v2;
	int v3;
	int f;
	int ramp;
	int soc;
	int timer;
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
	const int adj;
	const int total;
	enum dstate state;
	int power;
	int delta;
	int load;
	int p1;
	int p2;
	int p3;
	int noresponse;
	time_t override;
	ramp_function_t *ramp;
};

// program of the day
typedef struct potd_t {
	const char *name;
	device_t **devices;
} potd_t;

// set device function signatures
int ramp_heater(device_t *device, int power);
int ramp_boiler(device_t *device, int power);
int ramp_akku(device_t *device, int power);

// devices
static device_t a1 = { .name = "akku", .total = 0, .ramp = &ramp_akku, .adj = 0 }, *AKKU = &a1;
static device_t b1 = { .name = "boiler1", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t b2 = { .name = "boiler2", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t b3 = { .name = "boiler3", .total = 2000, .ramp = &ramp_boiler, .adj = 1 };
static device_t h1 = { .id = SWITCHBOX, .r = 1, .name = "küche", .total = 500, .ramp = &ramp_heater, .adj = 0 };
static device_t h2 = { .id = SWITCHBOX, .r = 2, .name = "wozi", .total = 500, .ramp = &ramp_heater, .adj = 0 };
static device_t h3 = { .id = PLUG5, .r = 0, .name = "schlaf", .total = 500, .ramp = &ramp_heater, .adj = 0 };
static device_t h4 = { .id = PLUG6, .r = 0, .name = "tisch", .total = 200, .ramp = &ramp_heater, .adj = 0 };

// all devices, needed for initialization
static device_t *DEVICES[] = { &a1, &b1, &b2, &b3, &h1, &h2, &h3, &h4, 0 };

// first charge akku, then boilers, then heaters
static device_t *DEVICES_MODEST[] = { &a1, &b1, &h1, &h2, &h3, &h4, &b2, &b3, 0 };

// steal all akku charge power
static device_t *DEVICES_GREEDY[] = { &h1, &h2, &h3, &h4, &b1, &b2, &b3, &a1, 0 };

// heaters, then akku, then boilers (catch remaining pv from secondary inverters or if akku is not able to consume all generated power)
static device_t *DEVICES_PLENTY[] = { &h1, &h2, &h3, &h4, &a1, &b1, &b2, &b3, 0 };

// force boiler heating first
static device_t *DEVICES_BOILERS[] = { &b1, &b2, &b3, &h1, &h2, &h3, &h4, &a1, 0 };
static device_t *DEVICES_BOILER1[] = { &b1, &a1, &b2, &b3, &h1, &h2, &h3, &h4, 0 };
static device_t *DEVICES_BOILER3[] = { &b3, &a1, &b1, &b2, &h1, &h2, &h3, &h4, 0 };

// define POTDs
static const potd_t MODEST = { .name = "MODEST", .devices = DEVICES_MODEST };
static const potd_t GREEDY = { .name = "GREEDY", .devices = DEVICES_GREEDY };
static const potd_t PLENTY = { .name = "PLENTY", .devices = DEVICES_PLENTY };
static const potd_t BOILERS = { .name = "BOILERS", .devices = DEVICES_BOILERS };
static const potd_t BOILER1 = { .name = "BOILER1", .devices = DEVICES_BOILER1 };
static const potd_t BOILER3 = { .name = "BOILER3", .devices = DEVICES_BOILER3 };
