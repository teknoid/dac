#include <linux/input-event-codes.h>
#include <unistd.h>
#include <wiringPi.h>

#include "mcp.h"
#include "utils.h"

#define GPIO_POWER		0

#define msleep(x) usleep(x*1000)

// WM8741 workaround: switch through all channels
static void workaround_channel() {
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEDOWN");
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEUP");
	xlog("WM8741 workaround channel");
}

// WM8741 workaround: touch volume
static void workaround_volume() {
	dac_volume_down();
	msleep(100);
	dac_volume_up();
	msleep(100);
	xlog("WM8741 workaround volume");
}

static void dac_on() {
	digitalWrite(GPIO_POWER, 1);
	mcp->dac_power = 1;
	xlog("switched DAC on");
	sleep(3);
	workaround_volume();
}

static void dac_off() {
	digitalWrite(GPIO_POWER, 0);
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
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEUP");
	xlog("VOL++");
}

void dac_volume_down() {
	lirc_send(LIRC_REMOTE, "KEY_VOLUMEDOWN");
	xlog("VOL--");
}

void dac_mute() {
}

void dac_unmute() {
}

void dac_source(int source) {
}

int dac_init() {
	pinMode(GPIO_POWER, OUTPUT);

	mcp->dac_power = digitalRead(GPIO_POWER);
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
	case KEY_PAUSE:
	case KEY_PLAY:
		workaround_volume();
		mpdclient_handle(c);
		break;
	case KEY_EJECTCD:
		workaround_channel();
		break;
	case KEY_SELECT:
		lirc_send(LIRC_REMOTE, "KEY_CHANNELUP");
		xlog("CHANNELUP");
		break;
	case KEY_POWER:
		dac_power();
		break;
	default:
		mpdclient_handle(c);
	}
}
