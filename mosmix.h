#define CHEMNITZ				"/tmp/CHEMNITZ.csv"
#define MARIENBERG				"/tmp/MARIENBERG.csv"
#define BRAUNSDORF				"/tmp/BRAUNSDORF.csv"

#define MOSMIX_COLUMNS			5

// hexdump -v -e '22 "%6d ""\n"' /tmp/fronius-mosmix-history.bin
#define MOSMIX_HISTORY			"/tmp/fronius-mosmix-history.bin"
#define MOSMIX_HISTORY_CSV		"/run/mcp/mosmix-history.csv"
#define MOSMIX_TOMORROW_CSV		"/run/mcp/mosmix-tomorrow.csv"
#define MOSMIX_TODAY_CSV		"/run/mcp/mosmix-today.csv"

typedef struct _mosmix mosmix_t;
#define MOSMIX_SIZE		(sizeof(mosmix_t) / sizeof(int))
#define MOSMIX_HEADER	" Rad1h SunD1 mppt1 mppt2 mppt3 mppt4 base1 base2 base3 base4  exp1  exp2  exp3  exp4  err1  err2  err3  err4  fac1  fac2  fac3  fac4"
struct _mosmix {
	// mosmix raw values
	int Rad1h;
	int SunD1;
	// actual pv
	int mppt1;
	int mppt2;
	int mppt3;
	int mppt4;
	// base value calculated from mosmix raw values
	int base1;
	int base2;
	int base3;
	int base4;
	// expected pv calculated from base
	int exp1;
	int exp2;
	int exp3;
	int exp4;
	// error calculated actual vs. expected
	int err1;
	int err2;
	int err3;
	int err4;
	// factors calculated actual vs. base
	int fac1;
	int fac2;
	int fac3;
	int fac4;
};

// needed for migration
typedef struct mosmix_old_t {
	int Rad1h;
	int SunD1;
	int base;
	int exp1;
	int mppt1;
	int err1;
	int fac1;
	int exp2;
	int mppt2;
	int err2;
	int fac2;
	int exp3;
	int mppt3;
	int err3;
	int fac3;
	int exp4;
	int mppt4;
	int err4;
	int fac4;
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

void mosmix_store_state();
void mosmix_load_state();
void mosmix_dump_today(struct tm *now);
void mosmix_dump_tomorrow(struct tm *now);
void mosmix_dump_history_today(struct tm *now);
void mosmix_dump_history_full(struct tm *now);
void mosmix_dump_history_noon();
void mosmix_clear_today_tomorrow();
void mosmix_store_csv();
void mosmix_mppt(struct tm *now, int mppt1, int mppt2, int mppt3, int mppt4);
void mosmix_collect(struct tm *now, int *today, int *tomorrow, int *sod, int *eod);
void mosmix_survive(struct tm *now, int min, int *hours, int *from, int *to);
void mosmix_heating(struct tm *now, int min, int *hours, int *from, int *to);
void mosmix_24h(int day, mosmix_csv_t *sum);
int mosmix_load(const char *filename);
