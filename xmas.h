// light on: ↑earlier, ↓later
#define XMAS_SUNDOWN	100

// light off: ↑later ↓earlier
#define XMAS_SUNRISE	200

typedef struct timing_t {
	int active;								// enabled / disabled
	int wday;								// weekday
	int on_h;								// soonest switch on hour
	int on_m;								// soonest switch on minute
	int off_h;								// latest switch off hour
	int off_m;								// latest switch off minute
} timing_t;

static const timing_t timings[] = {
	{ 1, 1, 15, 00, 22, 00 }, // Monday
	{ 1, 2, 15, 00, 22, 00 },
	{ 1, 3, 15, 00, 22, 00 },
	{ 1, 4, 15, 00, 22, 00 },
	{ 1, 5, 15, 00, 23, 00 },
	{ 1, 6, 15, 00, 23, 00 },
	{ 1, 0, 15, 00, 23, 00 }, // Sunday
};
