#include <stdio.h>
#include <stdlib.h>
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

static void lirc_send(unsigned long m) {
	unsigned long mask = 1 << 31;

	// sync
	gpio_set(GPIO_LIRC_TX, 0);
	delay_micros(TH1);
	gpio_set(GPIO_LIRC_TX, 1);
	delay_micros(TH2);

	while (mask) {
		gpio_set(GPIO_LIRC_TX, 0);
		delay_micros(T1);
		gpio_set(GPIO_LIRC_TX, 1);

		if (m & mask)
			delay_micros(T01); // 1
		else
			delay_micros(T00); // 0

		mask = mask >> 1;
	}
	gpio_set(GPIO_LIRC_TX, 0);
	delay_micros(T1);
	gpio_set(GPIO_LIRC_TX, 1);

	usleep(100000);
}

// WM8741 workaround: switch through all channels
static void workaround_channel() {
	lirc_send(KEY_CUP);
	lirc_send(KEY_CUP);
	lirc_send(KEY_CUP);
	lirc_send(KEY_CUP);
	lirc_send(KEY_VDOWN);
	lirc_send(KEY_VUP);
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
	lirc_send(KEY_VUP);
}

void dac_volume_down() {
	xlog("VOL--");
	lirc_send(KEY_VDOWN);
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
		lirc_send(KEY_CUP);
		xlog("CHANNELUP");
		break;
	case 182: // KEY_REDO is defined different in curses.h !!!
	case KEY_TIME:
		if (!mcp->dac_power) {
			gpio_set(GPIO_POWER, 1);
			mcp->dac_power = 1;
		} else {
			gpio_set(GPIO_POWER, 0);
			mcp->dac_power = 0;
		}
		break;
	case KEY_POWER:
		dac_power();
		break;
	default:
		mpdclient_handle(c);
	}
}
