#include "shelly.h"

// relay 1 of HOFLICHT is triggered by HOFLICHT buttons 1 & 2, and has a timer of 120 seconds
static shelly_t shelly_config[] = {
	{ HOFLICHT, 1, HOFLICHT, 1, HOFLICHT, 2, 0, 0, 120, 0 },
};
