#define STATIONS					100
#define CLIENTS						100

#define LINEBUF						2048

typedef struct client_t {
	time_t ts;
	uint64_t mac;
	unsigned int count;
	int signal;
	int channel;
	char tag;
	char ssid[64];
	char oui[64];
	char name[64];
	char smac[18];
} client_t;

typedef struct station_t {
	time_t ts;
	uint64_t mac;
	unsigned int count;
	int signal;
	int channel;
	char ssid[64];
	char oui[64];
	char name[64];
	char smac[18];
	client_t clients[CLIENTS];
	int dirty;
} station_t;

typedef struct connection_t {
	pthread_t thread;
	struct sockaddr address;
	socklen_t addr_len;
	char ip[16];
	int sock;
	FILE *stream;
	char line[LINEBUF];
	char line_dump[LINEBUF];
} connection_t;
