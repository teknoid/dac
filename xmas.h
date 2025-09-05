// the remote control unit
#define WHITE1				1

typedef struct xmas_t {
	unsigned int id;
	unsigned int relay;
} xmas_t;

// these tasmota devices are controlled via XMAS
static const xmas_t devices[] = {
	{ PLUG2, 0 },
	{ PLUG3, 0 },
	{ PLUG4, 0 },
	{ CARPORT, 1 },
	{ SCHUPPEN, 1 },
	{ SWITCHBOX, 4 },
};

typedef struct xmas_timing_t {
	int active;								// enabled / disabled
	int wday;								// weekday
	int on_h;								// soonest switch on hour
	int on_m;								// soonest switch on minute
	int off_h;								// latest switch off hour
	int off_m;								// latest switch off minute
	int remote;								// index of remote control unit
	char channel;							// channel of remote control unit
} xmas_timing_t;

static const xmas_timing_t timings[] = {
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
