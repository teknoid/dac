#define CHEMNITZ				"CHEMNITZ.csv"
#define MARIENBERG				"MARIENBERG.csv"
#define BRAUNSDORF				"BRAUNSDORF.csv"

#define MOSMIX_COLUMNS			6

// hexdump -v -e '19 "%6d ""\n"' /var/lib/mcp/solar-mosmix-history.bin
#define MOSMIX_HISTORY			"solar-mosmix-history.bin"

// hexdump -v -e '16 "%6d ""\n"' /var/lib/mcp/solar-mosmix-factors.bin
#define MOSMIX_FACTORS			"solar-mosmix-factors.bin"

// csvfilter.sh /run/mcp/mosmix-history.csv 12
#define MOSMIX_HISTORY_CSV		"mosmix-history.csv"
#define MOSMIX_FACTORS_CSV		"mosmix-factors.csv"
#define MOSMIX_TODAY_CSV		"mosmix-today.csv"
#define MOSMIX_TOMORROW_CSV		"mosmix-tomorrow.csv"

typedef struct _mosmix mosmix_t;
#define MOSMIX_SIZE		(sizeof(mosmix_t) / sizeof(int))
// CSV					1     2     3     4     5     6     7     8     9    10    11    12    13    14    15    16    17    18    19    20
#define MOSMIX_HEADER	" Rad1h SunD1   TTT mppt1 mppt2 mppt3 mppt4  exp1  exp2  exp3  exp4 diff1 diff2 diff3 diff4  err1  err2  err3  err4"
struct _mosmix {
	// mosmix raw values
	int Rad1h;
	int SunD1;
	int TTT;
	// actual pv
	int mppt1;
	int mppt2;
	int mppt3;
	int mppt4;
	// expected pv calculated from mosmix forecast
	int exp1;
	int exp2;
	int exp3;
	int exp4;
	// error calculated actual - expected
	int diff1;
	int diff2;
	int diff3;
	int diff4;
	// error calculated actual / expected
	int err1;
	int err2;
	int err3;
	int err4;
};

// needed for migration
typedef struct mosmix_old_t {
	// mosmix raw values
	int Rad1h;
	int SunD1;
	int TTT;
	// actual pv
	int mppt1;
	int mppt2;
	int mppt3;
	int mppt4;
	// expected pv calculated from mosmix forecast
	int exp1;
	int exp2;
	int exp3;
	int exp4;
	// error calculated actual - expected
	int diff1;
	int diff2;
	int diff3;
	int diff4;
	// error calculated actual / expected
	int err1;
	int err2;
	int err3;
	int err4;
} mosmix_old_t;

typedef struct mosmix_csv_t {
	// from file
	int idx;
	time_t ts;
	float TTT;
	int Rad1h;
	int SunD1;
	int RSunD;
} mosmix_csv_t;

typedef struct _factor factor_t;
#define FACTOR_SIZE		(sizeof(factor_t) / sizeof(int))
#define FACTOR_HEADER	"    r1    r2    r3    r4    s1    s2    s3    s4    e1    e2    e3    e4"
struct _factor {
	int r1;
	int r2;
	int r3;
	int r4;
	int s1;
	int s2;
	int s3;
	int s4;
	int e1;
	int e2;
	int e3;
	int e4;
};

void mosmix_store_state();
void mosmix_load_state(struct tm *now);
void mosmix_factors(int wait);
void mosmix_dump_today(struct tm *now);
void mosmix_dump_tomorrow(struct tm *now);
void mosmix_dump_history_today(struct tm *now);
void mosmix_dump_history_full(struct tm *now);
void mosmix_dump_history_hours(int hour);
void mosmix_store_csv();
void mosmix_mppt(struct tm *now, int mppt1, int mppt2, int mppt3, int mppt4);
void mosmix_collect(struct tm *now, int *today, int *tomorrow, int *sod, int *eod);
int mosmix_survive(struct tm *now, int loads[], int baseload, int extra);
int mosmix_heating(struct tm *now, int power);
void mosmix_24h(int day, mosmix_csv_t *sum);
int mosmix_load(struct tm *now, const char *filename, int clear);
