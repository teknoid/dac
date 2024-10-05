#define CHEMNITZ			"/tmp/CHEMNITZ.csv"
#define MARIENBERG			"/tmp/MARIENBERG.csv"
#define BRAUNSDORF			"/tmp/BRAUNSDORF.csv"

#define MOSMIX_MIN			1.0
#define MOSMIX_MAX			5.0
#define MOSMIX_DEFAULT		3.0

#define MOSMIX_COLUMNS		5

typedef struct _mosmix mosmix_t;

struct _mosmix {
	int idx;
	time_t ts;
	float TTT;
	int Rad1h;
	int SunD1;
};

int mosmix_load(const char *filename);
void mosmix_eod(mosmix_t *sum, time_t now_ts);
void mosmix_24h(mosmix_t *sum, time_t now_ts, int day);
int mosmix_survive(time_t now_ts, int rad1h_min);
mosmix_t* mosmix_current_slot(time_t now_ts);

