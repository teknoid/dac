// light on: ↑earlier, ↓later
#define SUNDOWN			100

// light off: ↑later ↓earlier
#define SUNRISE			200

// these tasmota devices will be in XMAS mode
#define DEVICES			PLUG1, PLUG2, PLUG3, PLUG4, PLUG5, PLUG6, PLUG7, PLUG8, PLUG9

// the remote control unit
#define WHITE1				1

typedef struct timing_t {
	int active;								// enabled / disabled
	int wday;								// weekday
	int on_h;								// soonest switch on hour
	int on_m;								// soonest switch on minute
	int off_h;								// latest switch off hour
	int off_m;								// latest switch off minute
	int remote;								// index of remote control unit
	char channel;							// channel of remote control unit
} timing_t;

static const timing_t timings[] = {
	{ 1, 1, 15, 00, 22, 00, WHITE1, 'A' }, // Monday
	{ 1, 2, 15, 00, 22, 00, WHITE1, 'A' },
	{ 1, 3, 15, 00, 22, 00, WHITE1, 'A' },
	{ 1, 4, 15, 00, 22, 00, WHITE1, 'A' },
	{ 1, 5, 15, 00, 23, 00, WHITE1, 'A' },
	{ 1, 6, 15, 00, 23, 00, WHITE1, 'A' },
	{ 1, 0, 15, 00, 23, 00, WHITE1, 'A' }, // Sunday
};

void xmas_on();
void xmas_off();
