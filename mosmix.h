#define CHEMNITZ				"/tmp/CHEMNITZ.csv"
#define MARIENBERG				"/tmp/MARIENBERG.csv"
#define BRAUNSDORF				"/tmp/BRAUNSDORF.csv"

#define MOSMIX_COLUMNS			5

typedef struct _mosmix mosmix_t;
#define MOSMIX_SIZE		(sizeof(mosmix_t) / sizeof(int))
#define MOSMIX_HEADER	" Rad1h SunD1     x   exp   act   fac"
struct _mosmix {
	int Rad1h;
	int SunD1;
	int x;
	int expected;
	int actual;
	int factor;
};

typedef struct mosmix_file_t {
	// from file
	int idx;
	time_t ts;
	float TTT;
	int Rad1h;
	int SunD1;
	int RSunD;
} mosmix_file_t;

void mosmix_dump_today();
void mosmix_dump_tomorrow();
void mosmix_takeover();
void mosmix_calculate(int *today, int *tomorrow);
void mosmix_update(int hour, int actual);
void mosmix_sod_eod(int hour, mosmix_t *sod, mosmix_t *eod);
void mosmix_survive(time_t now_ts, int rad1h_min, int *hours, int *from, int *to);
void mosmix_24h(time_t now_ts, int day, mosmix_file_t *sum);
int mosmix_load(time_t now_ts, const char *filename);
