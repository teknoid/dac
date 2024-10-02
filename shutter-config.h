#include "tasmota.h"

#ifdef SHUTTER_MAIN
#define TEMP				23.2
#define HUMI				33
#define LUMI				35000
#else
#define TEMP				sensors->sht31_temp
#define HUMI				sensors->sht31_humi
#define LUMI				sensors->bh1750_lux_mean
#endif

typedef struct shutter_t {
	const char *name;
	const unsigned int id;
	const int down;
	const int down_from;
	const int down_to;
	int lock_down;
	int lock_up;
} shutter_t;

typedef struct potd_t {
	const char *name;
	const int months[12];
	const int lumi;
	const int temp;
	shutter_t *shutters[];
} potd_t;

// east side
static shutter_t e1 = { .name = "rollo-kueche", .id = ROLLO_KUECHE, .down = 60, .down_from = 7, .down_to = 11 };
static shutter_t e2 = { .name = "rollo-o", .id = ROLLO_O, .down = 30, .down_from = 7, .down_to = 11 };

// south side
static shutter_t s1 = { .name = "rollo-oma", .id = ROLLO_OMA, .down = 50, .down_from = 10, .down_to = 17 };
static shutter_t s2 = { .name = "rollo-so", .id = ROLLO_SO, .down = 50, .down_from = 10, .down_to = 17 };
static shutter_t s3 = { .name = "rollo-sw", .id = ROLLO_SW, .down = 50, .down_from = 10, .down_to = 17 };

// west side
static shutter_t w1 = { .name = "rollo-w", .id = ROLLO_W, .down = 50, .down_from = 16, .down_to = 20 };

// program of the day for summer and winter            j  f  m  a  m  j  j  a  s  o  n  d
static potd_t SUMMER = { .name = "summer", .months = { 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0 }, .shutters = { &e1, &e2, &s1, &s2, &s3, &w1, NULL }, .lumi = 20000, .temp = 25 };
static potd_t WINTER = { .name = "winter", .months = { 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1 }, .shutters = { &e1, &e2, &s1, &s2, &s3, &w1, NULL }, .lumi = 50, .temp = -1 };

