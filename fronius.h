#define CHUNK_SIZE 		2048

#define	HEATERS			"plug9"
#define HEATER_WATT		600

#define BOILERS			"boiler1", "boiler2", "boiler3"
#define BOILER_WATT		2000
#define BOILER_STANDBY	20

// Fronius API is slow --> timings <5s make no sense
#define WAIT_OFFLINE	300
#define WAIT_STANDBY	60
#define WAIT_KEEP		30
#define WAIT_RAMPUP		5
#define WAIT_RAMPDOWN	5

#define STANDBY_EXPIRE	3600 / WAIT_STANDBY

#define PV_HISTORY		16

// 0-10 --> 50 watt steps !!!
#define PHASE_ANGLES_600	0, 2395, 2524, 2693, 2845, 2980, 3098, 3199, 3283, 3350, 3400,\
							3440, 3480, 3520, 3560, 3600, 3640, 3680, 3720, 3760, 3800,\
							3835, 3870, 3905, 3940, 3975, 4010, 4045, 4080, 4115, 4150,\
							4185, 4220, 4255, 4290, 4325, 4360, 4395, 4430, 4465, 4500,\
							4540, 4580, 4620, 4660, 4700, 4740, 4780, 4820, 4860, 4900,\
							4945, 4990, 5035, 5080, 5125, 5170, 5215, 5260, 5305, 5350,\
							5400, 5450, 5500, 5550, 5600, 5650, 5700, 5750, 5800, 5850,\
							5915, 5980, 6045, 6110, 6175, 6240, 6305, 6370, 6435, 6500,\
							6580, 6660, 6740, 6820, 6900, 6980, 7060, 7140, 7220, 7300,\
							7380, 7450, 7600, 7750, 7900, 8000, 8250, 8500, 8800, 10000

#define PHASE_ANGLES_2000	0, 2750, 2875, 2975, 3075, 3150, 3225, 3300, 3350, 3400, 3450,\
							3500, 3550, 3600, 3650, 3700, 3750, 3800, 3850, 3900, 3950,\
							3980, 4010, 4040, 4070, 4100, 4130, 4160, 4190, 4220, 4250,\
							4290, 4330, 4370, 4410, 4450, 4490, 4530, 4570, 4610, 4650,\
							4690, 4730, 4770, 4810, 4850, 4890, 4930, 4970, 5010, 5050,\
							5095, 5140, 5185, 5230, 5275, 5320, 5365, 5410, 5455, 5500,\
							5550, 5600, 5650, 5700, 5750, 5800, 5850, 5900, 5950, 6000,\
							6050, 6100, 6150, 6200, 6250, 6300, 6350, 6400, 6450, 6500,\
							6580, 6660, 6740, 6820, 6900, 6980, 7060, 7140, 7220, 7300,\
							7380, 7450, 7600, 7750, 7900, 8000, 8250, 8500, 8800, 10000

//#define URL			"http://fronius/solar_api/v1/GetMeterRealtimeData.cgi?Scope=Device&DeviceId=0"
#define URL				"http://fronius/solar_api/v1/GetPowerFlowRealtimeData.fcgi"

typedef struct get_response_t {
	char *buffer;
	size_t len;
	size_t buflen;
} get_response_t;

typedef struct device_t {
	const char *name;
	const char *addr;
	int active;
	int override;
	int power;
} device_t;

void fronius_override(int index);
