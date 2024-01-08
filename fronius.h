#define CHUNK_SIZE 		2048

#define BOILERS			"boiler1", "boiler2", "boiler3"

#define BOILER_STANDBY	20

#define WAIT_OFFLINE	60
#define WAIT_STANDBY	30
#define WAIT_KEEP		10
#define WAIT_RAMPUP		5
#define WAIT_RAMPDOWN	5
#define WAIT_OVERRIDE	60 * 10

#define STANDBY_EXPIRE	3600 / WAIT_STANDBY

//#define URL			"http://fronius/solar_api/v1/GetMeterRealtimeData.cgi?Scope=Device&DeviceId=0"
#define URL				"http://fronius/solar_api/v1/GetPowerFlowRealtimeData.fcgi"

typedef struct get_request_t {
	char *buffer;
	size_t len;
	size_t buflen;
} get_request_t;

typedef struct boiler_t {
	const char *name;
	const char *addr;
	unsigned int load;
} boiler_t;

void override();
