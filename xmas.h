// light on: ↑earlier, ↓later
#define SUNDOWN			100

// light off: ↑later ↓earlier
#define SUNRISE			200

// the remote control unit
#define WHITE1				1

typedef struct xmas_t {
	unsigned int id;
	unsigned int relay;
} xmas_t;

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

void xmas_on();
void xmas_off();
