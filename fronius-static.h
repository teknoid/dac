#define CHUNK_SIZE 				2048

// Fronius API is slow --> timings <5s make no sense
#define WAIT_OFFLINE			900
#define WAIT_STANDBY			300
#define WAIT_KEEP				60
#define WAIT_NEXT				5

#define STANDBY					20
#define STANDBY_EXPIRE			3600 / WAIT_STANDBY

#define KEEP_FROM				25
#define KEEP_TO					75

#define PV_HISTORY				32

#define URL_METER			"http://fronius/solar_api/v1/GetMeterRealtimeData.cgi?Scope=Device&DeviceId=0"
#define URL_FLOW			"http://fronius/solar_api/v1/GetPowerFlowRealtimeData.fcgi"

typedef struct get_response_t {
	char *buffer;
	size_t len;
	size_t buflen;
} get_response_t;

typedef int (*set_function_t)(void*, int);

typedef struct device_t {
	const char *name;
	const char *addr;
	unsigned const int *phase_angle;
	int maximum;
	int adjustable;
	int greedy;
	int active;
	int override;
	int standby;
	int power;
	set_function_t set_function;
} device_t;

int fronius_set_heater(void *ptr, int power);
int fronius_set_boiler(void *ptr, int power);

// SSR control voltage for 0..100% power
// TODO Tabelle in ESP32 integrieren und direktaufruf zusätzlich über prozentuale angabe

#define PHASE_ANGLES_BOILER1 \
0, 2340, 2350, 2370, 2410, 2450, 2510, 2560, 2620, 2660, 2710, \
2760, 2800, 2840, 2880, 2920, 2950, 2990, 3030, 3060, 3100, \
3140, 3170, 3210, 3250, 3270, 3310, 3350, 3380, 3420, 3440, \
3490, 3530, 3550, 3600, 3640, 3680, 3700, 3740, 3780, 3810, \
3860, 3900, 3940, 3970, 4030, 4070, 4110, 4150, 4200, 4240, \
4280, 4330, 4380, 4420, 4480, 4530, 4590, 4660, 4740, 4800, \
4850, 4890, 4950, 5010, 5060, 5120, 5180, 5240, 5310, 5380, \
5440, 5510, 5580, 5640, 5710, 5790, 5880, 5960, 6070, 6190, \
6320, 6490, 6620, 6700, 6780, 6920, 7050, 7260, 7400, 7550, \
7780, 8190, 8490, 9850, 9930, 9999, 9999, 9999, 9999, 10000
static const unsigned int phase_angle1[] = { PHASE_ANGLES_BOILER1 };

#define PHASE_ANGLES_BOILER2 \
0, 2760, 2900, 3010, 3080, 3140, 3200, 3250, 3300, 3350, 3400, \
3450, 3500, 3550, 3580, 3620, 3660, 3700, 3750, 3780, 3820, \
3870, 3890, 3940, 3980, 4010, 4050, 4080, 4110, 4150, 4190, \
4220, 4260, 4300, 4330, 4380, 4420, 4460, 4490, 4520, 4570, \
4610, 4640, 4680, 4730, 4780, 4810, 4860, 4900, 4930, 4980, \
5040, 5080, 5120, 5180, 5220, 5280, 5320, 5370, 5400, 5450, \
5530, 5590, 5630, 5670, 5740, 5770, 5850, 5910, 5970, 6030, \
6090, 6140, 6220, 6290, 6370, 6420, 6500, 6560, 6630, 6710, \
6780, 6900, 6980, 7090, 7200, 7310, 7440, 7600, 7780, 7900, \
8080, 8240, 8410, 8960, 9390, 9900, 9960, 9999, 9999, 10000
static const unsigned int phase_angle2[] = { PHASE_ANGLES_BOILER2 };

#define PHASE_ANGLES_BOILER3 \
0, 2470, 2600, 2710, 2790, 2850, 2910, 2970, 3010, 3060, 3110, \
3160, 3200, 3230, 3280, 3330, 3360, 3390, 3430, 3460, 3490, \
3530, 3570, 3600, 3640, 3660, 3700, 3740, 3770, 3800, 3840, \
3860, 3910, 3940, 3970, 4010, 4030, 4070, 4110, 4140, 4170, \
4190, 4230, 4280, 4300, 4350, 4390, 4440, 4480, 4520, 4560, \
4600, 4650, 4700, 4730, 4780, 4820, 4880, 4930, 4960, 5030, \
5070, 5130, 5160, 5210, 5290, 5330, 5360, 5440, 5480, 5550, \
5610, 5660, 5690, 5770, 5840, 5900, 5960, 6040, 6090, 6170, \
6260, 6320, 6390, 6500, 6600, 6670, 6800, 6900, 7020, 7140, \
7300, 7430, 7570, 7680, 7850, 8940, 9750, 9800, 9900, 10000
static const unsigned int phase_angle3[] = { PHASE_ANGLES_BOILER3 };

// configuration for cloudy weather: priority is warm water in boiler1+2 and then akku charging
static device_t c1 = { .name = "boiler1", .maximum = 2000, .set_function = &fronius_set_boiler, .adjustable = 1, .phase_angle = phase_angle1, .greedy = 1 };
static device_t c2 = { .name = "boiler2", .maximum = 2000, .set_function = &fronius_set_boiler, .adjustable = 1, .phase_angle = phase_angle2, .greedy = 1 };
static device_t c3 = { .name = "boiler3", .maximum = 2000, .set_function = &fronius_set_boiler, .adjustable = 1, .phase_angle = phase_angle3 };
static device_t c4 = { .name = "plug9", .maximum = 700, .set_function = &fronius_set_heater };
static device_t *CONFIG_CLOUDY[] = { &c1, &c2, &c3, &c4 };

// configuration for sunny weather: akku will be fully anyway at the end of day
static device_t s1 = { .name = "plug9", .maximum = 700, .set_function = &fronius_set_heater, .greedy = 1 };
static device_t s2 = { .name = "boiler1", .maximum = 2000, .set_function = &fronius_set_boiler, .adjustable = 1, .phase_angle = phase_angle1, .greedy = 1 };
static device_t s3 = { .name = "boiler2", .maximum = 2000, .set_function = &fronius_set_boiler, .adjustable = 1, .phase_angle = phase_angle2, .greedy = 1 };
static device_t s4 = { .name = "boiler3", .maximum = 2000, .set_function = &fronius_set_boiler, .adjustable = 1, .phase_angle = phase_angle3, .greedy = 1 };
static device_t *CONFIG_SUNNY[] = { &s1, &s2, &s3, &s4 };
