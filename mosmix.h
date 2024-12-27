#define CHEMNITZ				"/tmp/CHEMNITZ.csv"
#define MARIENBERG				"/tmp/MARIENBERG.csv"
#define BRAUNSDORF				"/tmp/BRAUNSDORF.csv"

#define MOSMIX_COLUMNS			5

// hexdump -v -e '15 "%6d ""\n"' /tmp/fronius-mosmix-*.bin
#define TODAY_FILE				"/tmp/fronius-mosmix-today.bin"
#define TOMORROW_FILE			"/tmp/fronius-mosmix-tomorrow.bin"

typedef struct _mosmix mosmix_t;
#define MOSMIX_SIZE		(sizeof(mosmix_t) / sizeof(int))
#define MOSMIX_HEADER	" Rad1h SunD1     X  exp1 mppt1  fac1  exp2 mppt2  fac2  exp3 mppt3  fac3  exp4 mppt4  fac4"
struct _mosmix {
	int Rad1h;
	int SunD1;
	int x;
	int exp1;
	int mppt1;
	int fac1;
	int exp2;
	int mppt2;
	int fac2;
	int exp3;
	int mppt3;
	int fac3;
	int exp4;
	int mppt4;
	int fac4;
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

void mosmix_store_state();
void mosmix_load_state();
void mosmix_takeover();
void mosmix_dump_today(int highlight);
void mosmix_dump_tomorrow(int highlight);
void mosmix_mppt(int hour, int mppt1, int mppt2, int mppt3, int mppt4);
void mosmix_expected(int hour, int *today, int *tomorrow, int *sod, int *eod);
void mosmix_survive(int hour, int min, int *hours, int *from, int *to);
void mosmix_heating(int hour, int min, int *hours, int *from, int *to);
void mosmix_24h(time_t now_ts, int day, mosmix_file_t *sum);
int mosmix_load(time_t now_ts, const char *filename);
