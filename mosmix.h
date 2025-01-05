#define CHEMNITZ				"/tmp/CHEMNITZ.csv"
#define MARIENBERG				"/tmp/MARIENBERG.csv"
#define BRAUNSDORF				"/tmp/BRAUNSDORF.csv"

#define MOSMIX_COLUMNS			5

// hexdump -v -e '19 "%6d ""\n"' /tmp/fronius-mosmix.bin
#define MOSMIX_FILE				"/tmp/fronius-mosmix.bin"
#define MOSMIX_FILE_CSV			"/tmp/fronius-mosmix.csv"

typedef struct _mosmix mosmix_t;
#define MOSMIX_SIZE		(sizeof(mosmix_t) / sizeof(int))
#define MOSMIX_HEADER	" Rad1h SunD1  base  exp1 mppt1  err1  fac1  exp2 mppt2  err2  fac2  exp3 mppt3  err3  fac3  exp4 mppt4  err4  fac4"
struct _mosmix {
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
};

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
void mosmix_mppt(struct tm *now, int mppt1, int mppt2, int mppt3, int mppt4);
void mosmix_expected(struct tm *now, int *today, int *tomorrow, int *sod, int *eod);
void mosmix_survive(struct tm *now, int min, int *hours, int *from, int *to);
void mosmix_heating(struct tm *now, int min, int *hours, int *from, int *to);
void mosmix_24h(int day, mosmix_csv_t *sum);
int mosmix_load(const char *filename);
