#include "solar.h"

#define HISTORY_SIZE			(24 * 7)

#define NOISE					10
#define DELTA					15
#define RAMP					35
#define SUSPICIOUS				500
#define EMERGENCY				1000
#define ENOUGH					2000

#define BASELOAD				(GSTATE_WINTER ? 300 : 200)
#define MINIMUM					(BASELOAD / 2)

#define SUMMER					(4 <= now->tm_mon && now->tm_mon <= 7) 									// May - August
#define WINTER					(now->tm_mon == 10 || now->tm_mon == 11 || now->tm_mon == 0)			// November, Dezember, Januar

#define MINLY					(now->tm_sec == 0)
#define HOURLY					(now->tm_min == 0)
#define DAILY					(now->tm_hour == 0)

// gstate flags
#define FLAG_CHARGE_AKKU		(1 << 12)
#define FLAG_HEATING			(1 << 13)
#define FLAG_WINTER				(1 << 14)
#define FLAG_SUMMER				(1 << 15)

#define GSTATE_CHARGE_AKKU		(gstate->flags & FLAG_CHARGE_AKKU)
#define GSTATE_HEATING			(gstate->flags & FLAG_HEATING)
#define GSTATE_WINTER			(gstate->flags & FLAG_WINTER)
#define GSTATE_SUMMER			(gstate->flags & FLAG_SUMMER)

// pstate flags
#define FLAG_DELTA				(1 << 0)
#define FLAG_VALID				(1 << 1)
#define FLAG_STABLE				(1 << 2)
#define FLAG_DISTORTION			(1 << 3)
#define FLAG_PV_FALLING			(1 << 4)
#define FLAG_PV_RISING			(1 << 5)
#define FLAG_BURNOUT			(1 << 6)
#define FLAG_EMERGENCY			(1 << 7)

#define FLAG_GRID_ULOAD			(1 << 12)
#define FLAG_GRID_DLOAD			(1 << 13)
#define FLAG_AKKU_DCHARGE		(1 << 14)
#define FLAG_OFFLINE			(1 << 15)

#define PSTATE_DELTA			(pstate->flags & FLAG_DELTA)
#define PSTATE_VALID			(pstate->flags & FLAG_VALID)
#define PSTATE_STABLE			(pstate->flags & FLAG_STABLE)
#define PSTATE_DISTORTION		(pstate->flags & FLAG_DISTORTION)
#define PSTATE_PV_RISING		(pstate->flags & FLAG_PV_RISING)
#define PSTATE_PV_FALLING		(pstate->flags & FLAG_PV_FALLING)
#define PSTATE_BURNOUT			(pstate->flags & FLAG_BURNOUT)
#define PSTATE_EMERGENCY		(pstate->flags & FLAG_EMERGENCY)

#define PSTATE_GRID_ULOAD		(pstate->flags & FLAG_GRID_ULOAD)
#define PSTATE_GRID_DLOAD		(pstate->flags & FLAG_GRID_DLOAD)
#define PSTATE_AKKU_DCHARGE		(pstate->flags & FLAG_AKKU_DCHARGE)
#define PSTATE_OFFLINE			(pstate->flags & FLAG_OFFLINE)

// dstate flags
#define FLAG_ALL_DOWN			(1 << 0)
#define FLAG_ALL_STANDBY		(1 << 1)
#define FLAG_ALL_UP				(1 << 2)
#define FLAG_CHECK_STANDBY		(1 << 15)

#define DSTATE_ALL_DOWN			(dstate->flags & FLAG_ALL_DOWN)
#define DSTATE_ALL_STANDBY		(dstate->flags & FLAG_ALL_STANDBY)
#define DSTATE_ALL_UP			(dstate->flags & FLAG_ALL_UP)
#define DSTATE_CHECK_STANDBY	(dstate->flags & FLAG_CHECK_STANDBY)

enum e_state {
	Disabled, Active, Charge, Discharge, Standby, Standby_Check, Active_Checked,
};

// device definitions
typedef struct _device device_t;
typedef int (ramp_function_t)(device_t*, int);
struct _device {
	const unsigned int id;
	const unsigned int r;
	const char *name;
	const char *addr;
	const char *host;
	const int adj;
	const int total;
	const int from;
	const int to;
	enum e_state state;
	int power;
	int delta;
	int load;
	int min;
	int p1;
	int p2;
	int p3;
	int noresponse;
	time_t override;
	ramp_function_t *ramp;
};

// self and meter counter with access pointers
typedef struct _counter counter_t;
#define COUNTER_SIZE		(sizeof(counter_t) / sizeof(int))
#define COUNTER_HEADER	" ↑grid ↓grid mppt1 mppt2 mppt3 mppt4"
struct _counter {
	unsigned int produced;
	unsigned int consumed;
	unsigned int mppt1;
	unsigned int mppt2;
	unsigned int mppt3;
	unsigned int mppt4;
};
// self counter 1-5
#define CS_NOW					(&counter[0])
#define CS_LAST					(&counter[1])
#define CS_NULL					(&counter[2])
#define CS_HOUR					(&counter[3])
#define CS_DAY					(&counter[4])
// meter counter 6-10
#define CM_NOW					(&counter[5])
#define CM_LAST					(&counter[6])
#define CM_NULL					(&counter[7])
#define CM_HOUR					(&counter[8])
#define CM_DAY					(&counter[9])

// 24/7 gstate history slots
typedef struct _gstate gstate_t;
#define GSTATE_SIZE		(sizeof(gstate_t) / sizeof(int))
#define GSTATE_HEADER	"    pv ↑grid ↓grid today  tomo   sod   eod nsurv nheat  load   soc  akku   ttl  succ  foca  surv   tin  tout flags"
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
	int load;
	int soc;
	int akku;
	int ttl;
	int success;
	int forecast;
	int survive;
	int temp_in;
	int temp_out;
	int flags;
};

// pstate history every second/minute/hour
typedef struct _pstate pstate_t;
#define PSTATE_SIZE		(sizeof(pstate_t) / sizeof(int))
#define PSTATE_HEADER	"    pv   Δpv   ∑pv  grid Δgrid ∑grid  akku   ac1   ac2  load Δload ∑load   dc1   dc2 mppt1 mppt2 mppt3 mppt4    p1    p2    p3    v1    v2    v3     f   soc   inv flags"
struct _pstate {
	int pv;
	int dpv;
	int sdpv;
	int grid;
	int dgrid;
	int sdgrid;
	int akku;
	int ac1;
	int ac2;
	int load;
	int dload;
	int sdload;
	int dc1;
	int dc2;
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
	int soc;
	int inv;
	int flags;
};

// dstate
typedef struct _dstate dstate_t;
#define DSTATE_SIZE		(sizeof(dstate_t) / sizeof(int))
#define DSTATE_HEADER	"  ramp steal xload dload  lock flags"
struct _dstate {
	int ramp;
	int steal;
	int xload;
	int dload;
	int lock;
	int flags;
};

// global counter and state pointer
extern counter_t counter[10];
extern gstate_t *gstate;
extern pstate_t *pstate;
extern dstate_t *dstate;

// mutex for updating / calculating pstate and counter
extern pthread_mutex_t collector_lock;

// implementations in *modbus.c / *api.c / *simulator.c / *utils.c

int akku_capacity();
int akku_charge_max();
int akku_discharge_max();
int akku_state();
int akku_get_min_soc();
void akku_set_min_soc();
int akku_standby(device_t *akku);
int akku_charge(device_t *akku, int limit);
int akku_discharge(device_t *akku, int limit);

void inverter_status(int *inv1, int *inv2);
