// hexdump -v -e '6 "%10d ""\n"' /var/lib/mcp/solar-counter.bin
#define COUNTER_H_FILE			"solar-counter-hours.bin"
#define COUNTER_FILE			"solar-counter.bin"

// hexdump -v -e '17 "%6d ""\n"' /var/lib/mcp/solar-gstate.bin
#define GSTATE_H_FILE			"solar-gstate-hours.bin"
#define GSTATE_M_FILE			"solar-gstate-minutes.bin"
#define GSTATE_FILE				"solar-gstate.bin"

// hexdump -v -e '30 "%6d ""\n"' /var/lib/mcp/solar-pstate*.bin
#define PSTATE_H_FILE			"solar-pstate-hours.bin"
#define PSTATE_M_FILE			"solar-pstate-minutes.bin"
#define PSTATE_S_FILE			"solar-pstate-seconds.bin"
#define PSTATE_FILE				"solar-pstate.bin"

// CSV files for gnuplot
#define GSTATE_TODAY_CSV		"gstate-today.csv"
#define GSTATE_WEEK_CSV			"gstate-week.csv"
#define GSTATE_M_CSV			"gstate-minutes.csv"
#define PSTATE_M_CSV			"pstate-minutes.csv"
#define LOADS_CSV				"loads.csv"

// JSON files for webui
#define PSTATE_JSON				"pstate.json"
#define GSTATE_JSON				"gstate.json"
#define DSTATE_JSON				"dstate.json"
#define POWERFLOW_JSON			"powerflow.json"

#define DSTATE_TEMPLATE			"{\"name\":\"%s\", \"state\":%d, \"power\":%d, \"total\":%d, \"load\":%d}"
#define POWERFLOW_TEMPLATE		"{\"common\":{\"datestamp\":\"01.01.2025\",\"timestamp\":\"00:00:00\"},\"inverters\":[{\"BatMode\":1,\"CID\":0,\"DT\":0,\"E_Total\":1,\"ID\":1,\"P\":1,\"SOC\":%f}],\"site\":{\"BackupMode\":false,\"BatteryStandby\":false,\"E_Day\":null,\"E_Total\":1,\"E_Year\":null,\"MLoc\":0,\"Mode\":\"bidirectional\",\"P_Akku\":%d,\"P_Grid\":%d,\"P_Load\":%d,\"P_PV\":%d,\"rel_Autonomy\":100.0,\"rel_SelfConsumption\":100.0},\"version\":\"13\"}"

#define MOSMIX3X24				"SOLAR mosmix Rad1h/SunD1/RSunD today %d/%d/%d tomorrow %d/%d/%d tomorrow+1 %d/%d/%d"
#define GNUPLOT					"/usr/bin/gnuplot -p /home/hje/workspace-cpp/dac/misc/solar.gp"

#define SUMMER					(4 <= now->tm_mon && now->tm_mon <= 8) 									// May - September
#define WINTER					(now->tm_mon == 10 || now->tm_mon == 11 || now->tm_mon == 0)			// November, Dezember, Januar
#define MINLY					(now->tm_sec == 0)
#define HOURLY					(now->tm_min == 0)
#define DAILY					(now->tm_hour == 0)

#define AKKU_BURNOUT			1
#define BASELOAD				(WINTER ? 300 : 200)
#define MINIMUM					(BASELOAD / 2)

#define AKKU_CAPACITY_SOC(soc)	(AKKU_CAPACITY * (soc) / 1000)
#define AKKU_CHARGING			(AKKU->state == Charge)

#define NOISE					10
#define RAMP_WINDOW				35
#define SUSPICIOUS				500
#define EMERGENCY				1000
#define ENOUGH					2000

#define OVERRIDE				600

#define STANDBY_NORESPONSE		5

#define WAIT_INVALID			3
#define WAIT_RESPONSE			5
#define WAIT_AKKU_CHARGE		30
#define WAIT_BURNOUT			1800

#define ARRAY_SIZE(x) 			(sizeof(x) / sizeof(x[0]))

#define GREEDY_MODEST(d)		(d->greedy ? "greedy" : "modest")

// gstate flags
#define FLAG_HEATING			(1 << 0)

#define GSTATE_HEATING			(gstate->flags & FLAG_HEATING)

// pstate flags
#define FLAG_DELTA				(1 << 0)
#define FLAG_VALID				(1 << 1)
#define FLAG_STABLE				(1 << 2)
#define FLAG_DISTORTION			(1 << 3)
#define FLAG_CHECK_STANDBY		(1 << 4)
#define FLAG_BURNOUT			(1 << 5)
#define FLAG_EMERGENCY			(1 << 6)

#define FLAG_ALL_UP				(1 << 12)
#define FLAG_ALL_STANDBY		(1 << 13)
#define FLAG_ALL_DOWN			(1 << 14)
#define FLAG_OFFLINE			(1 << 15)

#define PSTATE_DELTA			(pstate->flags & FLAG_DELTA)
#define PSTATE_VALID			(pstate->flags & FLAG_VALID)
#define PSTATE_STABLE			(pstate->flags & FLAG_STABLE)
#define PSTATE_DISTORTION		(pstate->flags & FLAG_DISTORTION)
#define PSTATE_CHECK_STANDBY	(pstate->flags & FLAG_CHECK_STANDBY)
#define PSTATE_BURNOUT			(pstate->flags & FLAG_BURNOUT)
#define PSTATE_EMERGENCY		(pstate->flags & FLAG_EMERGENCY)

#define PSTATE_ALL_UP			(pstate->flags & FLAG_ALL_UP)
#define PSTATE_ALL_STANDBY		(pstate->flags & FLAG_ALL_STANDBY)
#define PSTATE_ALL_DOWN			(pstate->flags & FLAG_ALL_DOWN)
#define PSTATE_OFFLINE			(pstate->flags & FLAG_OFFLINE)

#define DD						(*dd)
#define UP						(*dd)->total
#define DOWN					(*dd)->total * -1

#define HISTORY					(24 * 7)

enum dstate {
	Disabled, Active, Active_Checked, Standby, Standby_Check, Charge, Discharge
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
	const int from;
	const int to;
	enum dstate state;
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
static counter_t counter_history[HISTORY], counter[10];
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
// history
#define COUNTER_HOUR_NOW		(&counter_history[24 * now->tm_wday + now->tm_hour])

// 24/7 gstate history slots and access pointers
typedef struct _gstate gstate_t;
#define GSTATE_SIZE		(sizeof(gstate_t) / sizeof(int))
#define GSTATE_HEADER	"    pv ↑grid ↓grid today  tomo   sod   eod nsurv nheat  load   soc  akku   ttl  succ  surv  heat flags"
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
	int survive;
	int heating;
	int flags;
};
static gstate_t gstate_history[HISTORY], gstate_minutes[60], gstate_current, *gstate = &gstate_current;
#define GSTATE_MIN_NOW			(&gstate_minutes[now->tm_min])
#define GSTATE_MIN_LAST1		(&gstate_minutes[now->tm_min > 0 ? now->tm_min - 1 : 59])
#define GSTATE_HOUR_NOW			(&gstate_history[24 * now->tm_wday + now->tm_hour])
#define GSTATE_HOUR_LAST		(&gstate_history[24 * now->tm_wday + now->tm_hour - (now->tm_wday == 0 && now->tm_hour ==  0 ?  24 * 7 - 1 : 1)])
#define GSTATE_HOUR_NEXT		(&gstate_history[24 * now->tm_wday + now->tm_hour + (now->tm_wday == 6 && now->tm_hour == 23 ? -24 * 7 + 1 : 1)])
#define GSTATE_TODAY			(&gstate_history[24 * now->tm_wday])
#define GSTATE_YDAY				(&gstate_history[24 * (now->tm_wday > 0 ? now->tm_wday - 1 : 6)])
#define GSTATE_HOUR(h)			(&gstate_history[24 * now->tm_wday + (h)])
#define GSTATE_YDAY_HOUR(h)		(&gstate_history[24 * (now->tm_wday > 0 ? now->tm_wday - 1 : 6) + (h)])
#define GSTATE_DAY_HOUR(d, h)	(&gstate_history[24 * (d) + (h)])

// needed for migration
typedef struct gstate_old_t {
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
	int survive;
	int heating;
} gstate_old_t;

// pstate history every second/minute/hour and access pointers
typedef struct _pstate pstate_t;
#define PSTATE_SIZE		(sizeof(pstate_t) / sizeof(int))
#define PSTATE_HEADER	"    pv   Δpv   ∑pv  grid Δgrid ∑grid  akku  ac1   ac2  load Δload ∑load xload dxlod  dc1   dc2 mppt1 mppt2 mppt3 mppt4    p1    p2    p3    v1    v2    v3     f  ramp   soc flags"
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
	int xload;
	int dxload;
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
	int ramp;
	int soc;
	int flags;
};
static pstate_t pstate_seconds[60], pstate_minutes[60], pstate_hours[24], pstate_current, *pstate = &pstate_current;
#define PSTATE_NOW				(&pstate_seconds[now->tm_sec])
#define PSTATE_SEC_NEXT			(&pstate_seconds[now->tm_sec < 59 ? now->tm_sec + 1 : 0])
#define PSTATE_SEC_LAST1		(&pstate_seconds[now->tm_sec > 0 ? now->tm_sec - 1 : 59])
#define PSTATE_SEC_LAST2		(&pstate_seconds[now->tm_sec > 1 ? now->tm_sec - 2 : (now->tm_sec - 2 + 60)])
#define PSTATE_SEC_LAST3		(&pstate_seconds[now->tm_sec > 2 ? now->tm_sec - 3 : (now->tm_sec - 3 + 60)])
#define PSTATE_MIN_NOW			(&pstate_minutes[now->tm_min])
#define PSTATE_MIN_LAST1		(&pstate_minutes[now->tm_min > 0 ? now->tm_min - 1 : 59])
#define PSTATE_MIN_LAST2		(&pstate_minutes[now->tm_min > 1 ? now->tm_min - 2 : (now->tm_min - 2 + 60)])
#define PSTATE_MIN_LAST3		(&pstate_minutes[now->tm_min > 2 ? now->tm_min - 3 : (now->tm_min - 3 + 60)])
#define PSTATE_HOUR_NOW			(&pstate_hours[now->tm_hour])
#define PSTATE_HOUR_LAST1		(&pstate_hours[now->tm_hour > 0 ? now->tm_hour - 1 : 23])
#define PSTATE_HOUR(h)			(&pstate_hours[h])

// program of the day - choosen by mosmix forecast data
typedef struct potd_t {
	const char *name;
	device_t **devices;
} potd_t;
static potd_t *potd = 0;

// average loads over 24/7
static int loads[24];

static struct tm now_tm, *now = &now_tm;
static int lock = 0, sock = 0;

// mutex for updating / calculating pstate
pthread_mutex_t pstate_lock;

// set device function signatures
int ramp_heater(device_t *device, int power);
int ramp_boiler(device_t *device, int power);
int ramp_akku(device_t *device, int power);
