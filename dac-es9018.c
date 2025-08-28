#include <stdint.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "dac.h"
#include "mcp.h"
#include "mpd.h"
#include "gpio.h"
#include "utils.h"
#include "dac-es9018.h"

// local memory and global pointer
static dac_state_t dac_state;
dac_state_t *dac = &dac_state;

static void dac_on() {
	gpio_set(GPIO_DAC_POWER, 1);
	dac->dac_power = 1;
	xlog("DAC switched on");

	msleep(500);

	gpio_set(GPIO_EXT_POWER, 1);
	dac->ext_power = 1;
	xlog("DAC switched EXT on");
}

static void dac_off() {
	gpio_set(GPIO_EXT_POWER, 0);
	dac->ext_power = 0;
	xlog("DAC switched EXT off");

	msleep(500);

	gpio_set(GPIO_DAC_POWER, 0);
	dac->dac_power = 0;
	xlog("DAC switched off");
}

void dac_power() {
	if (!dac->dac_power) {
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
	if (!dac->dac_power)
		return;

	gpio_configure(GPIO_VOL_UP, 1, 0, 1);
	msleep(50);
	gpio_set(GPIO_VOL_UP, 0);
	msleep(50);
	gpio_configure(GPIO_VOL_UP, 0, 0, 0);
	xlog("DAC vol++");
}

void dac_volume_down() {
	if (!dac->dac_power)
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
		dac->switch4 = gpio_toggle(GPIO_SWITCH4);
		break;
	default:
		mpdclient_handle(c);
	}
}

static int init() {
	dac->ir_active = 1;

	gpio_configure(GPIO_VOL_UP, 0, 0, 0);
	gpio_configure(GPIO_VOL_DOWN, 0, 0, 0);

	dac->switch2 = gpio_configure(GPIO_SWITCH2, 1, 0, -1);
	xlog("DAC SWITCH2 is %s", dac->switch2 ? "ON" : "OFF");

	dac->switch3 = gpio_configure(GPIO_SWITCH3, 1, 0, -1);
	xlog("DAC SWITCH3 is %s", dac->switch3 ? "ON" : "OFF");

	dac->switch4 = gpio_configure(GPIO_SWITCH4, 1, 0, -1);
	xlog("DAC SWITCH4 is %s", dac->switch4 ? "ON" : "OFF");

	dac->dac_power = gpio_configure(GPIO_DAC_POWER, 1, 0, -1);
	xlog("DAC power is %s", dac->dac_power ? "ON" : "OFF");

	dac->ext_power = gpio_configure(GPIO_EXT_POWER, 1, 0, -1);
	xlog("DAC EXT power is %s", dac->ext_power ? "ON" : "OFF");

	return 0;
}

static void stop() {
}

MCP_REGISTER(dac_es9018, 3, &init, &stop, NULL);
