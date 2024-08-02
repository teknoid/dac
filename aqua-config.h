const int VALVE1 = 1;

typedef struct aqua_t {
	const int v[4];		// valve 1..4
	const int hr[24];	// hours 0..24
	const int r;		// seconds
	const int t;		// temperature above
	const int h;		// humidity below
	const int l;		// luminousity above
	const char *n;		// name
} aqua_t;

static const aqua_t v1h1r1 = { .v = { 1, 0, 0, 0 }, .hr = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, .r = 060, .t = 30, .h = 40, .l = 0, .n =
		"v1h1r1" };

static const aqua_t v1h3r2 = { .v = { 1, 0, 0, 0 }, .hr = { 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0 }, .r = 120, .t = 20, .h = 60, .l = 0, .n =
		"v1h3r2" };

static const aqua_t v1h6r5 = { .v = { 1, 0, 0, 0 }, .hr = { 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 }, .r = 300, .t = 10, .h = 80, .l = 0, .n =
		"v1h6r5" };

// program of the day list
static const aqua_t POTD[] = { v1h1r1, v1h3r2, v1h6r5 };
