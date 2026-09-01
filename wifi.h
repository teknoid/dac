#define STATIONS					100
#define CLIENTS						100

#define LINEBUF						2048

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
	int count;
	int signal;
	int channel;
	char ssid[64];
	char oui[64];
	char name[64];
	char smac[18];
	client_t clients[CLIENTS];
	client_t *pclients[CLIENTS + 1];
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
