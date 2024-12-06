#define CHEMNITZ			"/tmp/CHEMNITZ.csv"
#define MARIENBERG			"/tmp/MARIENBERG.csv"
#define BRAUNSDORF			"/tmp/BRAUNSDORF.csv"

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
void mosmix_24h(time_t now_ts, int day, mosmix_t *sum);
void mosmix_sod_eod(time_t now_ts, mosmix_t *sod, mosmix_t *eod);
float mosmix_noon(time_t now_ts, mosmix_t *forenoon, mosmix_t *afternoon);
int mosmix_survive(time_t now_ts, int rad1h_min);
mosmix_t* mosmix_current_slot(time_t now_ts);
