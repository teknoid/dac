#define STATIONS				128
#define CLIENTS					256
#define CLIENTS4				(CLIENTS * 4)

#define DESCRIPTION				64
#define DESCRIPTION2			(DESCRIPTION * 2)

#define META_BEACON				"BEACON"
#define META_ZOMBIE				"ZOMBIE"
#define META_CACHE				"CACHE"
#define META_BLACK				"BLACK"
#define META_NAME				"NAME"
#define META_HOME				"HOME"

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

typedef struct meta_station_t {
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
	client_t clients[CLIENTS4];
	client_t *pclients[CLIENTS4 + 1];
} meta_station_t;
size_t META_STATION_SIZE = sizeof(meta_station_t);

typedef struct wifi_t {
	int station_count;
	station_t station[STATIONS];
	station_t *pstation[STATIONS + 1];
	meta_station_t beacon;
	meta_station_t zombie;
	meta_station_t cache;
	meta_station_t black;
	meta_station_t name;
	meta_station_t home;
	meta_station_t *pmeta[7];
} wifi_t;
size_t WIFI_SIZE = sizeof(wifi_t);

