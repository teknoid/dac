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
	int RSunD;
};

int mosmix_load(time_t now_ts, const char *filename);
void mosmix_24h(time_t now_ts, int day, mosmix_t *sum);
void mosmix_sod_eod(time_t now_ts, mosmix_t *sod, mosmix_t *eod);
void mosmix_survive(time_t now_ts, int rad1h_min, int *hours, int *from, int *to);
float mosmix_noon();
mosmix_t* mosmix_current_slot(time_t now_ts);
