#define VALVES			4

#ifdef AQUA_MAIN
#define TEMP				23.2
#define HUMI				33
#define LUMI				35000
#else
#define TEMP				sensors->sht31_temp
#define HUMI				sensors->sht31_humi
#define LUMI				sensors->bh1750_lux_mean
#endif

typedef struct aqua_t {
	const int v[VALVES];	// valve 0..VALVES-1
	const int hr[24];		// hours 0..23
	const int r;			// seconds
	const int t;			// temperature above
	const int h;			// humidity below
	const int l;			// luminousity above
	const char *n;			// name
} aqua_t;

static const aqua_t v0h1r1 = { .v = { 1, 0, 0, 0 }, .hr = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, .r = 60, .t = 25, .h = 40, .l = 0, .n =
		"v0h1r1" };

static const aqua_t v0h3r2 = { .v = { 1, 0, 0, 0 }, .hr = { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0 }, .r = 120, .t = 20, .h = 60, .l = 0, .n =
		"v0h3r2" };

static const aqua_t v0h6r3 = { .v = { 1, 0, 0, 0 }, .hr = { 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 }, .r = 180, .t = 15, .h = 80, .l = 0, .n =
		"v0h6r3" };

// program of the day
static const aqua_t POTD[] = { v0h1r1, v0h3r2, v0h6r3 };
