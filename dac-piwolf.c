#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <linux/input-event-codes.h>

#include "mcp.h"
#include "gpio.h"
#include "utils.h"

#define GPIO_POWER		"GPIO17"
#define GPIO_LIRC_TX	"GPIO22"

#define TH1				9020
#define	TH2				4460

#define T1				580
#define T01				1660
#define T00				550

#define KEY_VUP			0x00FD20DF
#define KEY_VDOWN		0x00FD10EF
#define KEY_CUP			0x00FD609F
#define KEY_CDOWN		0x00FD50AF

// WM8741 workaround: switch through all channels
static void workaround_channel() {
	gpio_lirc(GPIO_LIRC_TX, KEY_CUP);
	gpio_lirc(GPIO_LIRC_TX, KEY_CUP);
	gpio_lirc(GPIO_LIRC_TX, KEY_CUP);
	gpio_lirc(GPIO_LIRC_TX, KEY_CUP);
	gpio_lirc(GPIO_LIRC_TX, KEY_VDOWN);
	gpio_lirc(GPIO_LIRC_TX, KEY_VUP);
	xlog("WM8741 workaround channel");
}

// WM8741 workaround: touch volume
static void workaround_volume() {
	dac_volume_down();
	dac_volume_up();
	xlog("WM8741 workaround volume");
}

static void dac_on() {
	gpio_set(GPIO_POWER, 1);
	mcp->dac_power = 1;
	xlog("switched DAC on");
	sleep(3);
	workaround_volume();
}

static void dac_off() {
	gpio_set(GPIO_POWER, 0);
	mcp->dac_power = 0;
	xlog("switched DAC off");
}

void dac_power() {
	if (!mcp->dac_power) {
		dac_on();
		mpdclient_handle(KEY_PLAY);
	} else {
		mpdclient_handle(KEY_STOP);
		dac_off();
	}
}

void dac_volume_up() {
	xlog("VOL++");
	gpio_lirc(GPIO_LIRC_TX, KEY_VUP);
}

void dac_volume_down() {
	xlog("VOL--");
	gpio_lirc(GPIO_LIRC_TX, KEY_VDOWN);
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

int dac_init() {
	elevate_realtime(3);
	init_micros();

	if (gpio_init() < 0)
		return -1;

	// LIRC TX is low_active
	gpio_configure(GPIO_LIRC_TX, 1, 0, 1);

	mcp->dac_power = gpio_configure(GPIO_POWER, 1, 0, -1);
	if (mcp->dac_power) {
		xlog("DAC  power is ON");
	} else {
		xlog("DAC  power is OFF");
	}

	return 0;
}

void dac_close() {
}

void dac_handle(int c) {
	switch (c) {
	case KEY_VOLUMEUP:
		dac_volume_up();
		break;
	case KEY_VOLUMEDOWN:
		dac_volume_down();
		break;
	case KEY_PAUSE:
	case KEY_PLAY:
		workaround_volume();
		mpdclient_handle(c);
		break;
	case KEY_EJECTCD:
		workaround_channel();
		break;
	case KEY_SELECT:
		gpio_lirc(GPIO_LIRC_TX, KEY_CUP);
		xlog("CHANNELUP");
		break;
	case KEY_POWER:
		dac_power();
		break;
	default:
		mpdclient_handle(c);
	}
}
