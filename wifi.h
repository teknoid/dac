#define STATIONS					128
#define CLIENTS						256

#define LINEBUF						2048
#define DESCRIPTION					64

typedef struct client_t {
	uint64_t mac;
	time_t ts;
	int count;
	int signal;
	int channel;
	char tag;
	char ssid[DESCRIPTION];
	char ou[DESCRIPTION];
	char name[DESCRIPTION];
	char smac[18];
} client_t;
size_t CLIENT_SIZE = sizeof(client_t);

typedef struct station_t {
	uint64_t mac;
	time_t ts;
	int dirty;
	int ccount;
	int count;
	int signal;
	int channel;
	char ssid[DESCRIPTION];
	char ou[DESCRIPTION];
	char name[DESCRIPTION];
	char smac[18];
	client_t clients[CLIENTS];
	client_t *pclients[CLIENTS + 1];
} station_t;
size_t STATION_SIZE = sizeof(station_t);

typedef struct connection_t {
	pthread_t thread;
	struct sockaddr address;
	socklen_t addr_len;
	char ip[16];
	int sock;
	FILE *stream;
	char line[LINEBUF];
	char line_dump[LINEBUF];
	unsigned int line_count;
} connection_t;
size_t CONNECTION_SIZE = sizeof(connection_t);

typedef struct description_t {
	uint64_t mac;
	char description[DESCRIPTION];
} description_t;

typedef struct wifi_t {
	int command;
	pthread_t command_thread;
	int server;
	int server_fd;
	pthread_t server_thread;
} wifi_t;
