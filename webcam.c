#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "tasmota.h"
#include "webcam.h"
#include "utils.h"
#include "mcp.h"

#define LUMI				sensors->bh1750_raw2

static int webcam_on;

static void webcam_start() {
	system(WEBCAM_START);
	webcam_on = 1;
	xlog("executed %s", WEBCAM_START);
}

static void start_reset() {
	system(WEBCAM_START_RESET);
	webcam_on = 1;
	xlog("executed %s", WEBCAM_START_RESET);
}

static void webcam_stop() {
	system(WEBCAM_STOP);
	webcam_on = 0;
	xlog("executed %s", WEBCAM_STOP);
}

static void stop_timelapse() {
	system(WEBCAM_STOP_TIMELAPSE);
	webcam_on = 0;
	xlog("executed %s", WEBCAM_STOP_TIMELAPSE);
}

static void webcam() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// wait till network & nfs available
	sleep(15);

	// state unknown (e.g. system startup) --> check need for switching on
	if (LUMI > WEBCAM_SUNRISE)
		webcam_start();
	else
		webcam_stop();

	while (1) {
		LOCALTIME

		int afternoon = now->tm_hour < 12 ? 0 : 1;

		if (afternoon && webcam_on) {
			// evening and on --> check if need to switch off
			// xlog("awaiting WEBCAM_SUNDOWN at %i", value);
			if (LUMI < WEBCAM_SUNDOWN) {
				xlog("reached WEBCAM_SUNDOWN at bh1750_raw2=%d", LUMI);
				stop_timelapse();
			}
		}

		if (!afternoon && !webcam_on) {
			// morning and off --> check if need to switch on
			// xlog("awaiting WEBCAM_SUNRISE at %i", value);
			if (LUMI > WEBCAM_SUNRISE) {
				xlog("reached WEBCAM_SUNRISE at bh1750_raw2=%d", LUMI);
				start_reset();
			}
		}

		sleep(60);
	}
}

static int init() {
	webcam_on = 0;
	return 0;
}

static void stop() {
	webcam_stop();
}

MCP_REGISTER(webcam, 6, &init, &stop, &webcam);
