#define STATIONS					100
#define CLIENTS						100

typedef struct client_t {
	uint64_t mac;
	time_t ts;
	int count;
	int signal;
	int channel;
	char tag;
	char ssid[64];
	char oui[64];
	char name[64];
	char smac[18];
} client_t;

typedef struct station_t {
	uint64_t mac;
	time_t ts;
	int dirty;
	int count;
	int signal;
	int channel;
	char ssid[64];
	char oui[64];
	char name[64];
	char smac[18];
	client_t clients[CLIENTS];
} station_t;

typedef struct connection_t {
	pthread_t thread;
	struct sockaddr address;
	int addr_len;
	int sock;
} connection_t;
