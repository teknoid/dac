#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "dac.h"
#include "mcp.h"
#include "mpd.h"
#include "utils.h"

#ifndef MIXER
#define MIXER	"/usr/bin/amixer -q set Master"
#endif

int dummy_status;

static void dac_on() {

#ifdef GPIO_POWER
	gpio_set(GPIO_POWER, 1);
#endif

	mcp->dac_power = 1;
	xlog("DAC switched on");

	// wait for DAC init
	msleep(1000);
	mpdclient_handle(KEY_PLAY);
}

static void dac_off() {
	mpdclient_handle(KEY_STOP);

#ifdef GPIO_POWER
	gpio_set(GPIO_POWER, 0);
#endif

	xlog("DAC switched off");
	mcp->dac_power = 0;
}

void dac_power() {
	if (!mcp->dac_power)
		dac_on();
	else
		dac_off();
}

void dac_volume_up() {
	system(MIXER" 2%+");
	xlog("DAC vol++");
}

void dac_volume_down() {
	system(MIXER" 2%-");
	xlog("DAC vol--");
}

void dac_mute() {
	system(MIXER" mute");
	mcp->dac_mute = 1;
	xlog("DAC MUTE");
}

void dac_unmute() {
	system(MIXER" unmute");
	mcp->dac_mute = 0;
	xlog("DAC UNMUTE");
}

void dac_source(int source) {
	xlog("DAC dac_source");
}

int dac_status_get(const void *p1, const void *p2) {
	xlog("DAC dac_config_get");
	return dummy_status;
}

void dac_status_set(const void *p1, const void *p2, int value) {
	xlog("DAC dac_config_set");
	dummy_status = value;
}

void dac_handle(int c) {
	mpdclient_handle(c);
}

static int init() {
#ifdef GPIO_POWER
	mcp->dac_power = gpio_configure(GPIO_POWER, 1, 0, -1);
	xlog("DAC power is %s", mcp->dac_power ? "ON" : "OFF");
#endif

	return 0;
}

static void stop() {
}

MCP_REGISTER(dac_alsa, 3, &init, &stop, NULL);
