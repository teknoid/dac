const int VALVE1 = 1;

typedef struct aqua_t {
	const int v[4];		// valves 1..4
	const int h[24];	// hours 0..24
	const int rain;		// seconds
	const int temp;		// min. temperature
	const int humi;		// max. humidity
	const int lumi;		// min. luminousity
} aqua_t;

static const aqua_t a3h5m = { .v = { 1, 0, 0, 0 }, .h = { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0 }, .rain = 300, .humi = 80, .lumi = 0, .temp = 10 };
static const aqua_t a1h2m = { .v = { 1, 0, 0, 0 }, .h = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, .rain = 120, .humi = 30, .lumi = 0, .temp = 30 };

// program of the day list
static const aqua_t POTD[] = { a3h5m, a1h2m };
