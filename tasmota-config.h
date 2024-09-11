#include "tasmota.h"

//   1   2      3   4    5   6    7   8    9   10   11
// { id, relay, t1, t1b, t2, t2b, t3, t3b, t4, t4b, timer }

static const tasmota_config_t tasmota_config[] = {
	{ PLUG1, 0, KUECHE, 1, 0, 0, 0, 0, 0, 0, 0 },

	// relay 0 of HOFLICHT is triggered by HOFLICHT buttons 1 & 2, and has a timer of 120 seconds
	{ HOFLICHT, 0, HOFLICHT, 1, HOFLICHT, 2, 0, 0, 0, 0, 120 },

	// heater 150W has a timer of 1 hour
	{ PLUG5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 60 * 60 }
};
