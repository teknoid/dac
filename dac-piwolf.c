#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include "dac.h"
#include "mpd.h"
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

// local memory and global pointer
static dac_state_t dac_state;
dac_state_t *dac = &dac_state;

// WM8741 workaround: switch through all channels
static void workaround_channel() {
	gpio_lirc(GPIO_LIRC_TX, KEY_CUP);
	gpio_lirc(GPIO_LIRC_TX, KEY_CUP);
	gpio_lirc(GPIO_LIRC_TX, KEY_CUP);
	gpio_lirc(GPIO_LIRC_TX, KEY_CUP);
	gpio_lirc(GPIO_LIRC_TX, KEY_VDOWN);
	gpio_lirc(GPIO_LIRC_TX, KEY_VUP);
	xlog("DAC WM8741 workaround channel");
}

// WM8741 workaround: touch volume
static void workaround_volume() {
	dac_volume_down();
	dac_volume_up();
	xlog("DAC WM8741 workaround volume");
}

static void dac_on() {
	gpio_set(GPIO_POWER, 1);
	dac->dac_power = 1;
	xlog("DAC switched DAC on");
	sleep(3);
	workaround_volume();
}

static void dac_off() {
	gpio_set(GPIO_POWER, 0);
	dac->dac_power = 0;
	xlog("DAC switched DAC off");
}

void dac_power() {
	if (!dac->dac_power) {
		dac_on();
		mpdclient_handle(KEY_PLAY);
	} else {
		mpdclient_handle(KEY_STOP);
		dac_off();
	}
}

void dac_volume_up() {
	xlog("DAC vol++");
	gpio_lirc(GPIO_LIRC_TX, KEY_VUP);
}

void dac_volume_down() {
	xlog("DAC vol--");
	gpio_lirc(GPIO_LIRC_TX, KEY_VDOWN);
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
		xlog("DAC CHANNELUP");
		break;
	case KEY_POWER:
		dac_power();
		break;
	default:
		mpdclient_handle(c);
	}
}

static int init() {
	// elevate realtime priority for lirc sending
	if (elevate_realtime(3) < 0)
		return xerr("DAC Error elevating realtime");

	// LIRC TX is low_active
	gpio_configure(GPIO_LIRC_TX, 1, 0, 1);

	dac->dac_power = gpio_configure(GPIO_POWER, 1, 0, -1);
	if (dac->dac_power)
		xlog("DAC power is ON");
	else
		xlog("DAC power is OFF");

	return 0;
}

static void stop() {
}

MCP_REGISTER(dac_piwolf, 3, &init, &stop, NULL);
