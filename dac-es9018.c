#include <stdint.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "dac.h"
#include "mcp.h"
#include "mpd.h"
#include "gpio.h"
#include "utils.h"
#include "dac-es9018.h"

static void dac_on() {
	gpio_set(GPIO_DAC_POWER, 1);
	mcp->dac_power = 1;
	xlog("DAC switched on");

	msleep(500);

	gpio_set(GPIO_EXT_POWER, 1);
	mcp->ext_power = 1;
	xlog("DAC switched EXT on");
}

static void dac_off() {
	gpio_set(GPIO_EXT_POWER, 0);
	mcp->ext_power = 0;
	xlog("DAC switched EXT off");

	msleep(500);

	gpio_set(GPIO_DAC_POWER, 0);
	mcp->dac_power = 0;
	xlog("DAC switched off");
}

void dac_power() {
	if (!mcp->dac_power) {
		dac_on();
		// wait for XMOS USB boot
		msleep(5000);
		mpdclient_handle(KEY_PLAY);
	} else {
		mpdclient_handle(KEY_STOP);
		dac_off();
	}
}

void dac_volume_up() {
	if (!mcp->dac_power)
		return;

	gpio_configure(GPIO_VOL_UP, 1, 0, 1);
	msleep(50);
	gpio_set(GPIO_VOL_UP, 0);
	msleep(50);
	gpio_configure(GPIO_VOL_UP, 0, 0, 0);
	xlog("DAC vol++");
}

void dac_volume_down() {
	if (!mcp->dac_power)
		return;

	gpio_configure(GPIO_VOL_DOWN, 1, 0, 1);
	msleep(50);
	gpio_set(GPIO_VOL_DOWN, 0);
	msleep(50);
	gpio_configure(GPIO_VOL_DOWN, 0, 0, 0);
	xlog("DAC vol--");
}

void dac_mute() {
}

void dac_unmute() {
}

void dac_source(int source) {
}

int dac_status_get(const void *p1, const void *p2) {
	return 0;
}

void dac_status_set(const void *p1, const void *p2, int value) {
}

void dac_handle(int c) {
	switch (c) {
	case KEY_VOLUMEUP:
		dac_volume_up();
		break;
	case KEY_VOLUMEDOWN:
		dac_volume_down();
		break;
	case KEY_POWER:
		dac_power();
		break;
	case KEY_REDO:
	case KEY_TIME:
		mcp->switch4 = gpio_toggle(GPIO_SWITCH4);
		break;
	default:
		mpdclient_handle(c);
	}
}

static int init() {
	gpio_configure(GPIO_VOL_UP, 0, 0, 0);
	gpio_configure(GPIO_VOL_DOWN, 0, 0, 0);

	mcp->switch2 = gpio_configure(GPIO_SWITCH2, 1, 0, -1);
	xlog("DAC SWITCH2 is %s", mcp->switch2 ? "ON" : "OFF");

	mcp->switch3 = gpio_configure(GPIO_SWITCH3, 1, 0, -1);
	xlog("DAC SWITCH3 is %s", mcp->switch3 ? "ON" : "OFF");

	mcp->switch4 = gpio_configure(GPIO_SWITCH4, 1, 0, -1);
	xlog("DAC SWITCH4 is %s", mcp->switch4 ? "ON" : "OFF");

	mcp->dac_power = gpio_configure(GPIO_DAC_POWER, 1, 0, -1);
	xlog("DAC power is %s", mcp->dac_power ? "ON" : "OFF");

	mcp->ext_power = gpio_configure(GPIO_EXT_POWER, 1, 0, -1);
	xlog("DAC EXT power is %s", mcp->ext_power ? "ON" : "OFF");

	return 0;
}

static void stop() {
}

MCP_REGISTER(dac_es9018, 3, &init, &stop, NULL);
