#define STATIONS				128
#define CLIENTS					256
#define CLIENTS4				(CLIENTS * 4)

#define DESCRIPTION				64
#define DESCRIPTION2			(DESCRIPTION * 2)

#define SSID_BEACON				"BEACON"
#define SSID_ZOMBIE				"ZOMBIE"
#define SSID_CACHE				"CACHE"
#define SSID_BLACK				"BLACK"
#define SSID_NAME				"NAME"
#define SSID_HOME				"HOME"

#define __MAC \
	uint64_t mac; \
	time_t ts_first; \
	time_t ts_last; \
	int count; \
	int signal; \
	int channel; \
	char tag; \
	char ssid[DESCRIPTION]; \
	char ou[DESCRIPTION]; \
	char name[DESCRIPTION]; \
	char smac[18];

typedef struct mac_t {
	__MAC
} mac_t;
size_t MAC_SIZE = sizeof(mac_t);

typedef struct small_station_t {
	__MAC
	int dirty;
	int mcount;
	mac_t macs[CLIENTS];
	mac_t *pmacs[CLIENTS + 1];
} small_station_t;
size_t SMALL_STATION_SIZE = sizeof(small_station_t);

typedef struct big_station_t {
	__MAC
	int dirty;
	int mcount;
	mac_t macs[CLIENTS4];
	mac_t *pmacs[CLIENTS4 + 1];
} big_station_t;
size_t BIG_STATION_SIZE = sizeof(big_station_t);

typedef struct wifi_t {
	int station_count;
	small_station_t station[STATIONS];
	small_station_t *pstation[STATIONS + 1];
	big_station_t beacon;
	big_station_t zombie;
	big_station_t cache;
	big_station_t black;
	big_station_t name;
	big_station_t home;
	big_station_t *pmeta[7];
} wifi_t;
size_t WIFI_SIZE = sizeof(wifi_t);

