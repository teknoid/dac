#include <linux/input-event-codes.h>
#include <math.h>
#include <stdint.h>
#include <unistd.h>
#include <wiringPi.h>

#include "display.h"
#include "i2c.h"
#include "mcp.h"
#include "utils.h"

#define GPIO_EXT_POWER		0
#define GPIO_DAC_POWER		7

#define GPIO_LAMP			3

#define ADDR				0x48
#define REG_CONFIG			0x0f
#define REG_VOLUME			0x10
#define REG_STATUS			0x40

#define MCLK				100000000

#define msleep(x) usleep(x*1000)

static void gpio_toggle(int gpio) {
	int state = digitalRead(gpio);
	if (state == 0) {
		digitalWrite(gpio, 1); // on
		return;
	}
	if (state == 1) {
		digitalWrite(gpio, 0); // off
		return;
	}
}

static dac_signal_t dac_get_signal() {
	char value;
	i2c_read(ADDR, 100, &value);
	if (value == 2) {
		return pcm;
	} else if (value == 1) {
		return dsd;
	} else {
		return nlock;
	}
}

static int dac_get_fsr() {
	double dvalue;
	int rate;
	char v0;
	char v1;
	char v2;
	char v3;
	uint32_t dpll;
	uint64_t value;

	// Datasheet:
	// This value is latched on reading the LSB, so	register 66 must be read first to acquire the latest DPLL value.
	// The value is latched on LSB because the DPLL number can be changing as the I2C transactions are performed.
	i2c_read(ADDR, 0x42, &v0);
	i2c_read(ADDR, 0x43, &v1);
	i2c_read(ADDR, 0x44, &v2);
	i2c_read(ADDR, 0x45, &v3);

	dpll = v3 << 8;
	dpll = dpll << 8 | v2;
	dpll = dpll << 8 | v1;
	dpll = dpll << 8 | v0;

	value = dpll;
	value = (value * MCLK) / 0xffffffff;

	// xlog("DAC raw sample rate %d", value);
	dvalue = value / 100.0;
	dvalue = round(dvalue) / 10.0;

	rate = floor(dvalue);
	return rate;
}

static int dac_get_vol() {
	char value;
	int db;

	i2c_read(ADDR, REG_VOLUME, &value);
	db = (value / 2) * -1;
	return db;
}

static void dac_on() {
	char value;

	// power on
	digitalWrite(GPIO_DAC_POWER, 1);
	xlog("switched DAC on");
	mcp->dac_power = 1;
	msleep(100);

	// check status
	int timeout = 10;
	while (i2c_read(ADDR, REG_STATUS, &value) < 0) {
		msleep(100);
		if (--timeout == 0) {
			xlog("no answer, aborting.");
			return;
		}
		xlog("waiting for DAC status %d", timeout);
	}
	value >>= 2;
	if (value == 0b101010) {
		xlog("Found DAC ES9038Pro");
	} else if (value == 0b011100) {
		xlog("Found DAC ES9038Q2M");
	} else if (value == 0b101001 || value == 0b101000) {
		xlog("Found DAC ES9028Pro");
	}

	// initialize registers
	dac_mute();
	if (i2c_write(ADDR, REG_CONFIG, 0x07) < 0) {
		return;
	}
	if (i2c_write(ADDR, REG_VOLUME, 0x60) < 0) {
		return;
	}
	i2c_dump_reg(ADDR, REG_STATUS);

	dac_unmute();
	dac_update();

	// power on Externals
	digitalWrite(GPIO_EXT_POWER, 1);
	mcp->ext_power = 1;
	xlog("switched EXT on");
}

static void dac_off() {
	dac_mute();

	// power off Externals and wait to avoid speaker plop
	digitalWrite(GPIO_EXT_POWER, 0);
	mcp->ext_power = 1;
	xlog("switched EXT off");
	sleep(6);

	// power off DAC
	digitalWrite(GPIO_DAC_POWER, 0);
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
	if (!mcp->dac_power) {
		return;
	}

	char value;
	i2c_read(ADDR, REG_VOLUME, &value);
	if (value != 0x00)
		value--;
	if (value != 0x00)
		value--;
	i2c_write(ADDR, REG_VOLUME, value);
	int db = (value / 2) * -1;
	mcp->dac_volume = db;
	display_fullscreen_int(mcp->dac_volume);
	xlog("VOL++ %03d", db);
}

void dac_volume_down() {
	if (!mcp->dac_power) {
		return;
	}

	char value;
	i2c_read(ADDR, REG_VOLUME, &value);
	if (value != 0xf0)
		value++;
	if (value != 0xf0)
		value++;
	i2c_write(ADDR, REG_VOLUME, value);
	int db = (value / 2) * -1;
	mcp->dac_volume = db;
	display_fullscreen_int(mcp->dac_volume);
	xlog("VOL-- %03d", db);
}

void dac_mute() {
	if (!mcp->dac_power) {
		return;
	}

	i2c_set_bit(ADDR, 0x07, 0);
	mcp->dac_mute = 1;
	xlog("MUTE");
}

void dac_unmute() {
	if (!mcp->dac_power) {
		return;
	}

	i2c_clear_bit(ADDR, 0x07, 0);
	mcp->dac_mute = 0;
	xlog("UNMUTE");
}

void dac_update() {
	if (!mcp->dac_power) {
		return;
	}

	sleep(1); // wait for dac acquiring signal
	mcp->dac_volume = dac_get_vol();
	mcp->dac_signal = dac_get_signal();
	mcp->dac_rate = dac_get_fsr();
	mcp->dac_bits = 32; // DAC receives always 32bit from amanero - read from MPD

	switch (mcp->dac_signal) {
	case nlock:
		xlog("NLOCK");
		break;
	case pcm:
		xlog("PCM %d/%d %03ddB", mcp->mpd_bits, mcp->dac_rate, mcp->dac_volume);
		break;
	case dsd:
		xlog("DSD %d %03ddB", mcp->dac_rate, mcp->dac_volume);
		break;
	default:
		xlog("??? %d %03ddB", mcp->dac_rate, mcp->dac_volume);
		break;
	}
}

int dac_init() {
	if (i2c_init(I2C) < 0) {
		return -1;
	}

	pinMode(GPIO_EXT_POWER, OUTPUT);
	pinMode(GPIO_DAC_POWER, OUTPUT);
	pinMode(GPIO_LAMP, OUTPUT);

	mcp->dac_power = digitalRead(GPIO_DAC_POWER);
	if (mcp->dac_power) {
		xlog("DAC  power is ON");
	} else {
		xlog("DAC  power is OFF");
	}

	return 0;
}

void dac_close() {
	i2c_close();
}

void dac_handle(int c) {
	if (mcp->menu) {
		display_handle(c);
		return;
	}

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
	case KEY_TIME:
		gpio_toggle(GPIO_LAMP);
		break;
	case '\n':
	case KEY_SYSRQ:
	case KEY_F1:
		display_menu();
		break;
	default:
		mpdclient_handle(c);
	}
}
