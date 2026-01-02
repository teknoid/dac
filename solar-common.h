#include "tasmota.h"
#include "solar.h"

#define HISTORY_SIZE			(24 * 7)

#define NOISE					10
#define DELTA					20
#define RAMP					25
#define SUSPICIOUS				500
#define SPIKE					500
#define EMERGENCY				1000
#define EMERGENCY2X				2000

#define BASELOAD				(GSTATE_WINTER ? 300 : 200)

#define SUMMER					(4 <= now->tm_mon && now->tm_mon <= 7) 									// May - August
#define WINTER					(now->tm_mon == 10 || now->tm_mon == 11 || now->tm_mon == 0)			// November, Dezember, Januar

#define MINLY					(now->tm_sec == 0)
#define HOURLY					(now->tm_min == 0 && now->tm_sec == 0)
#define DAILY					(now->tm_hour == 0 && now->tm_min == 0 && now->tm_sec == 0)

#define AKKU_AVAILABLE			(gstate->soc > akku_get_min_soc() ? params->akku_capacity * (gstate->soc - akku_get_min_soc()) / 1000 : 0)
#define SURVIVE					1500

// common flags
#define FLAG_AKKU_DCHARGE		(1 << 10)
#define FLAG_GRID_DLOAD			(1 << 11)
#define FLAG_GRID_ULOAD			(1 << 12)
#define FLAG_PVFALL				(1 << 13)
#define FLAG_PVRISE				(1 << 14)
#define FLAG_STABLE				(1 << 15)

// gstate flags
#define FLAG_OFFLINE			(1 << 0)
#define FLAG_BURNOUT			(1 << 1)
#define FLAG_HEATING			(1 << 2)
#define FLAG_WINTER				(1 << 3)
#define FLAG_SUMMER				(1 << 4)
#define FLAG_CHARGE_AKKU		(1 << 5)
#define FLAG_FORCE_OFF			(1 << 6)

#define GSTATE_OFFLINE			(gstate->flags & FLAG_OFFLINE)
#define GSTATE_BURNOUT			(gstate->flags & FLAG_BURNOUT)
#define GSTATE_HEATING			(gstate->flags & FLAG_HEATING)
#define GSTATE_WINTER			(gstate->flags & FLAG_WINTER)
#define GSTATE_SUMMER			(gstate->flags & FLAG_SUMMER)
#define GSTATE_CHARGE_AKKU		(gstate->flags & FLAG_CHARGE_AKKU)
#define GSTATE_FORCE_OFF		(gstate->flags & FLAG_FORCE_OFF)
#define GSTATE_AKKU_DCHARGE		(gstate->flags & FLAG_AKKU_DCHARGE)
#define GSTATE_GRID_DLOAD		(gstate->flags & FLAG_GRID_DLOAD)
#define GSTATE_GRID_ULOAD		(gstate->flags & FLAG_GRID_ULOAD)
#define GSTATE_PVFALL			(gstate->flags & FLAG_PVFALL)
#define GSTATE_PVRISE			(gstate->flags & FLAG_PVRISE)
#define GSTATE_STABLE			(gstate->flags & FLAG_STABLE)

// pstate flags
#define FLAG_ACDELTA			(1 << 0)
#define FLAG_VALID				(1 << 1)
#define FLAG_EXTRAPOWER			(1 << 2)
#define FLAG_EMERGENCY			(1 << 3)

#define PSTATE_ACDELTA			(pstate->flags & FLAG_ACDELTA)
#define PSTATE_VALID			(pstate->flags & FLAG_VALID)
#define PSTATE_EXTRAPOWER		(pstate->flags & FLAG_EXTRAPOWER)
#define PSTATE_EMERGENCY		(pstate->flags & FLAG_EMERGENCY)
#define PSTATE_AKKU_DCHARGE		(pstate->flags & FLAG_AKKU_DCHARGE)
#define PSTATE_GRID_DLOAD		(pstate->flags & FLAG_GRID_DLOAD)
#define PSTATE_GRID_ULOAD		(pstate->flags & FLAG_GRID_ULOAD)
#define PSTATE_PVFALL			(pstate->flags & FLAG_PVFALL)
#define PSTATE_PVRISE			(pstate->flags & FLAG_PVRISE)
#define PSTATE_STABLE			(pstate->flags & FLAG_STABLE)

// dstate action flags
#define ACTION_FLAGS_MASK		0x00FF
#define FLAG_ACTION				(1 << 0)
#define FLAG_ACTION_RAMP		(1 << 1)
#define FLAG_ACTION_STANDBY		(1 << 2)
#define FLAG_ACTION_STEAL		(1 << 3)

// dstate state flags
#define STATE_FLAGS_MASK		0xFF00
#define FLAG_ALL_DOWN			(1 << 8)
#define FLAG_ALL_STANDBY		(1 << 9)
#define FLAG_ALL_UP				(1 << 10)

#define DSTATE_ACTION			(dstate->flags & FLAG_ACTION)
#define DSTATE_ACTION_RAMP		(dstate->flags & FLAG_ACTION_RAMP)
#define DSTATE_ACTION_STANDBY	(dstate->flags & FLAG_ACTION_STANDBY)
#define DSTATE_ACTION_STEAL		(dstate->flags & FLAG_ACTION_STEAL)
#define DSTATE_ALL_DOWN			(dstate->flags & FLAG_ALL_DOWN)
#define DSTATE_ALL_STANDBY		(dstate->flags & FLAG_ALL_STANDBY)
#define DSTATE_ALL_UP			(dstate->flags & FLAG_ALL_UP)

// device flags
#define FLAG_RESPONSE			(1 << 0)
#define FLAG_FORCE				(1 << 1)
#define FLAG_STANDBY_CHECK		(1 << 2)
#define FLAG_STANDBY_CHECKED	(1 << 3)

#define DEV_RESPONSE(d)			(d->flags & FLAG_RESPONSE)
#define DEV_FORCE(d)			(d->flags & FLAG_FORCE)
#define DEV_STANDBY_CHECK(d)	(d->flags & FLAG_STANDBY_CHECK)
#define DEV_STANDBY_CHECKED(d)	(d->flags & FLAG_STANDBY_CHECKED)

enum e_state {
	Disabled, Initial, Standby, Manual, Auto, Charge, Discharge
};

// parameters
typedef struct _params params_t;
#define PARAMS_SIZE		(sizeof(params_t) / sizeof(int))
struct _params {
	int akku_capacity;
	int akku_cmax;
	int akku_dmax;
	int akku_climit;
	int akku_dlimit;
	int baseload;
	int minimum;
};

// device definitions
typedef struct _device device_t;
typedef void (ramp_function_t)(device_t*);
struct _device {
	const unsigned int id;
	const unsigned int r;
	const char *name;
	const char *addr;
	const char *host;
	const int adj;
	const int from;
	const int to;
	const int min;
	enum e_state state;
	int flags;
	int total;
	int power;
	int steal;
	int load;
	int ramp;
	int climit;
	int dlimit;
	int p1;
	int p2;
	int p3;
	ramp_function_t *rf;
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
#define GSTATE_HEADER	"    pv pvmin pvavg pvmax ↑grid ↓grid today  tomo   sod   eod   soc   ttl  succ  foca avail  need  minu  surv flags"
struct _gstate {
	int pv;
	int pvmin;
	int pvavg;
	int pvmax;
	int produced;
	int consumed;
	int today;
	int tomorrow;
	int sod;
	int eod;
	int soc;
	int ttl;
	int success;
	int forecast;
	int akku;
	int needed;
	int minutes;
	int survive;
	int flags;
};

// pstate history every second/minute/hour
typedef struct _pstate pstate_t;
#define PSTATE_SIZE		(sizeof(pstate_t) / sizeof(int))
#define PSTATE_HEADER	"    pv  grid  akku   ac1   ac2   dc1   dc2 mppt1 mppt2 mppt3 mppt4    p1    p2    p3    v1    v2    v3     f  surp  load   rsl  ramp flags"
struct _pstate {
	int pv;
	int grid;
	int akku;
	int ac1;
	int ac2;
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
	int surp;
	int load;
	int rsl;
	int ramp;
	int flags;
};

// dstate
typedef struct _dstate dstate_t;
#define DSTATE_SIZE		(sizeof(dstate_t) / sizeof(int))
#define DSTATE_HEADER	" flags   inv  lock  resp  ramp steal cload rload"
struct _dstate {
	int flags;
	int inv;
	int lock;
	int resp;
	int ramp;
	int steal;
	int cload;
	int rload;
};

// global inverter pointers
extern device_t *inv1;
extern device_t *inv2;

// global counter, state and parameter pointer
extern counter_t counter[];
extern gstate_t *gstate;
extern pstate_t *pstate;
extern dstate_t *dstate;
extern params_t *params;

// mutex for updating / calculating pstate and counter
extern pthread_mutex_t collector_lock;

// implementations in *modbus.c / *api.c / *simulator.c / *utils.c

int akku_get_min_soc();
void akku_set_min_soc(int min);
void akku_state(device_t *akku);
int akku_standby(device_t *akku);
int akku_charge(device_t *akku);
int akku_discharge(device_t *akku);
