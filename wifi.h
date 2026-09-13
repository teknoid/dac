#define STATIONS				128
#define CLIENTS					256

#define DESCRIPTION				64

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

