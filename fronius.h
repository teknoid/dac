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

#define PV_HISTORY				16

#define URL_METER			"http://fronius/solar_api/v1/GetMeterRealtimeData.cgi?Scope=Device&DeviceId=0"
#define URL_FLOW			"http://fronius/solar_api/v1/GetPowerFlowRealtimeData.fcgi"

typedef struct get_response_t {
	char *buffer;
	size_t len;
	size_t buflen;
} get_response_t;

void fronius_override(const char *name);
