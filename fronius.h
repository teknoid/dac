#define CHUNK_SIZE 		2048

#define BOILER_STANDBY	20

//#define URL			"http://fronius/solar_api/v1/GetMeterRealtimeData.cgi?Scope=Device&DeviceId=0"
#define URL				"http://fronius/solar_api/v1/GetPowerFlowRealtimeData.fcgi"

typedef struct get_request_t {
	char *buffer;
	size_t len;
	size_t buflen;
} get_request_t;
