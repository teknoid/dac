#define CHEMNITZ				"/tmp/CHEMNITZ.csv"
#define MARIENBERG				"/tmp/MARIENBERG.csv"
#define BRAUNSDORF				"/tmp/BRAUNSDORF.csv"

#define MOSMIX_COLUMNS			5

typedef struct _mosmix mosmix_t;
#define MOSMIX_SIZE		(sizeof(mosmix_t) / sizeof(int))
#define MOSMIX_HEADER	"   idx                ts Rad1h SunD1 RSunD   exp   act   fac"
struct _mosmix {
	// from file
	int idx;
	time_t ts;
	int Rad1h;
	int SunD1;
	int RSunD;
	// calculations
	int expected;
	int actual;
	int factor;
};

void mosmix_dump_today();
void mosmix_dump_tomorrow();
void mosmix_takeover();
void mosmix_calculate(int *today, int *tomorrow);
void mosmix_update_time(time_t now_ts, int actual);
void mosmix_update_hour(int hour, int actual);
void mosmix_24h(time_t now_ts, int day, mosmix_t *sum);
void mosmix_sod_eod(time_t now_ts, mosmix_t *sod, mosmix_t *eod);
void mosmix_survive(time_t now_ts, int rad1h_min, int *hours, int *from, int *to);
int mosmix_load(time_t now_ts, const char *filename);
