#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>

#include <linux/input-event-codes.h>

#include "display.h"
#include "display-menu.h"

#include "dac.h"
#include "mpd.h"
#include "mcp.h"
#include "i2c.h"
#include "gpio.h"
#include "utils.h"
#include "dac-es9028.h"

#ifndef I2C
#define I2C				"/dev/i2c-0"
#endif

static int i2c;
static pthread_t thread;

static dac_signal_t dac_get_signal() {
	uint8_t value;
	i2c_read(i2c, ADDR, REG_STATUS, &value);
	if (value & 0x01) { // DPLL locked?
		i2c_read(i2c, ADDR, REG_SIGNAL, &value);
		switch (value & 0x0f) {
		case 0x01:
			return dsd;
		case 0x02:
			return pcm;
		case 0x04:
			return spdif;
		case 0x08:
			return dop;
		}
	}
	return nlock;
}

static dac_source_t dac_get_source() {
	uint8_t value;
	i2c_read(i2c, ADDR, REG_SOURCE, &value);
	switch (value) {
	case 0x70:
		return opt;
	case 0x80:
		return coax;
	default:
		return mpd;
	}
}

static int dac_get_fsr() {
	double dvalue;
	int rate;
	uint8_t v0;
	uint8_t v1;
	uint8_t v2;
	uint8_t v3;
	uint32_t dpll;
	uint64_t value;

	// Datasheet:
	// This value is latched on reading the LSB, so	register 66 must be read first to acquire the latest DPLL value.
	// The value is latched on LSB because the DPLL number can be changing as the I2C transactions are performed.
	i2c_read(i2c, ADDR, 0x42, &v0);
	i2c_read(i2c, ADDR, 0x43, &v1);
	i2c_read(i2c, ADDR, 0x44, &v2);
	i2c_read(i2c, ADDR, 0x45, &v3);

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
	uint8_t value;
	int db;

	i2c_read(i2c, ADDR, REG_VOLUME, &value);
	db = (value / 2) * -1;
	return db;
}

static void dac_on() {
	uint8_t value;

	// power on
	gpio_set(GPIO_DAC_POWER, 1);
	msleep(100);

	// check status
	int timeout = 10;
	while (i2c_read(i2c, ADDR, REG_STATUS, &value) < 0) {
		msleep(100);
		if (--timeout == 0) {
			xlog("no answer, aborting.");
			return;
		}
		xlog("waiting for DAC status %d", timeout);
	}
	value >>= 2;
	if (value == 0b101010)
		xlog("Found DAC ES9038Pro");
	else if (value == 0b011100)
		xlog("Found DAC ES9038Q2M");
	else if (value == 0b101001 || value == 0b101000)
		xlog("Found DAC ES9028Pro");

	// initialize registers
	dac_mute();
	if (i2c_write(i2c, ADDR, REG_CONFIG, 0x07) < 0)
		return;

	if (i2c_write(i2c, ADDR, REG_VOLUME, DEFAULT_VOLUME) < 0)
		return;

	dac_unmute();

	mcp->dac_power = 1;
	xlog("switched DAC on");

	// power on Externals
	gpio_set(GPIO_EXT_POWER, 1);
	mcp->ext_power = 1;
	xlog("switched EXT on");
}

static void dac_off() {
	dac_mute();
	display_fullscreen_string("---");

	// power off Externals and wait to avoid speaker plop
	gpio_set(GPIO_EXT_POWER, 0);
	mcp->ext_power = 0;
	xlog("switched EXT off");
	sleep(10);

	// power off DAC
	gpio_set(GPIO_DAC_POWER, 0);
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
	if (!mcp->dac_power)
		return;

	uint8_t value;
	i2c_read(i2c, ADDR, REG_VOLUME, &value);
	if (value != 0x00)
		value--;
	if (value != 0x00)
		value--;
	i2c_write(i2c, ADDR, REG_VOLUME, value);
	int db = (value / 2) * -1;
	mcp->dac_volume = db;
	display_fullscreen_number(mcp->dac_volume);
	xlog("VOL++ %03d", db);
}

void dac_volume_down() {
	if (!mcp->dac_power)
		return;

	uint8_t value;
	i2c_read(i2c, ADDR, REG_VOLUME, &value);
	if (value != 0xf0)
		value++;
	if (value != 0xf0)
		value++;
	i2c_write(i2c, ADDR, REG_VOLUME, value);
	int db = (value / 2) * -1;
	mcp->dac_volume = db;
	display_fullscreen_number(mcp->dac_volume);
	xlog("VOL-- %03d", db);
}

void dac_mute() {
	if (!mcp->dac_power)
		return;

	i2c_set_bit(i2c, ADDR, REG_MUTE, 0);
	mcp->dac_mute = 1;
	xlog("MUTE");
}

void dac_unmute() {
	if (!mcp->dac_power)
		return;

	i2c_clear_bit(i2c, ADDR, REG_MUTE, 0);
	mcp->dac_mute = 0;
	xlog("UNMUTE");
}

void dac_source_next() {
	switch (mcp->dac_source) {
	case mpd:
		return dac_source(opt);
	case opt:
		return dac_source(coax);
	case coax:
		return dac_source(mpd);
	}
}

void dac_source(int source) {
	if (!mcp->dac_power)
		return;

	switch (source) {
	case mpd:
		i2c_write(i2c, ADDR, REG_INPUT, 0x04); // auto detect DSD/PCM
		i2c_write(i2c, ADDR, REG_SOURCE, 0x00); // DATA_CLK
		dac_unmute();
		display_fullscreen_string("MPD");
		break;
	case opt:
		i2c_write(i2c, ADDR, REG_INPUT, 0x01); // SPDIF
		i2c_write(i2c, ADDR, REG_SOURCE, 0x70); // DATA7
		dac_unmute();
		display_fullscreen_string("OPT");
		break;
	case coax:
		i2c_write(i2c, ADDR, REG_INPUT, 0x01); // SPDIF
		i2c_write(i2c, ADDR, REG_SOURCE, 0x80); // DATA8
		dac_unmute();
		display_fullscreen_string("COX");
		break;
	}
	mcp->dac_source = source;
	mcp->dac_state_changed = 1;
}

int dac_status_get(const void *p1, const void *p2) {
	const menuconfig_t *config = p1;
	// const menuitem_t *item = p2;
	uint8_t value;
	i2c_read_bits(i2c, ADDR, config->reg, &value, config->mask);
	xlog("dac_status_get %02d, mask 0b%s, value %d", config->reg, printbits(config->mask, SPACEMASK), value);
	return value;
}

void dac_status_set(const void *p1, const void *p2, int value) {
	const menuconfig_t *config = p1;
	// const menuitem_t *item = p2;
	xlog("dac_status_set %02d, mask 0b%s, value %d", config->reg, printbits(config->mask, SPACEMASK), value);
	i2c_write_bits(i2c, ADDR, config->reg, value, config->mask);
}

void dac_handle(int c) {
	if (mcp->menu) {
		display_menu_mode();
		menu_handle(c);
		return;
	}

	switch (c) {
	case 0x41:
	case KEY_VOLUMEUP:
		dac_volume_up();
		break;
	case 0x42:
	case KEY_VOLUMEDOWN:
		dac_volume_down();
		break;
	case KEY_POWER:
		dac_power();
		break;
	case 182: // KEY_REDO is defined different in curses.h !!!
	case KEY_TIME:
		mcp->switch4 = gpio_toggle(GPIO_SWITCH4);
		break;
	case '\n':
	case 0x0d:
	case KEY_SYSRQ:
	case KEY_F1:
		display_menu_mode();
		menu_open(&m_main);
		break;
	case KEY_F4:
		dac_source_next();
		break;
	default:
		mpdclient_handle(c);
	}
}

static void* dac(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	char *s, s_mpd[] = "MPD", s_opt[] = "OPT", s_coax[] = "COAX";
	while (1) {
		msleep(250);

		if (!mcp->dac_power) {
			continue;
		}

		mcp->dac_source = dac_get_source();
		mcp->dac_signal = dac_get_signal();
		mcp->dac_volume = dac_get_vol();
		mcp->dac_rate = dac_get_fsr();

		if (!mcp->dac_state_changed) {
			continue;
		}
		mcp->dac_state_changed = 0;

		// print status only when state has changed
		switch (mcp->dac_source) {
		case mpd:
			s = s_mpd;
			break;
		case opt:
			s = s_opt;
			break;
		case coax:
			s = s_coax;
			break;
		}

		switch (mcp->dac_signal) {
		case dsd:
			xlog("[%s] DSD %d %03ddB", s, mcp->dac_rate, mcp->dac_volume);
			break;
		case pcm:
			xlog("[%s] PCM %d/%d %03ddB", s, mcp->mpd_bits, mcp->dac_rate, mcp->dac_volume);
			break;
		case spdif:
			xlog("[%s] SPDIF %d %03ddB", s, mcp->dac_rate, mcp->dac_volume);
			break;
		case dop:
			xlog("[%s] DOP %d %03ddB", s, mcp->dac_rate, mcp->dac_volume);
			break;
		default:
			xlog("[%s] NLOCK", s);
			mcp->dac_state_changed = 1; // try again
			break;
		}
	}
}

static int init() {
	if ((i2c = open(I2C, O_RDWR)) < 0)
		return xerr("error opening  %s", I2C);

	mcp->switch2 = gpio_configure(GPIO_SWITCH2, 1, 0, -1);
	xlog("SWITCH2 is %s", mcp->switch2 ? "ON" : "OFF");

	mcp->switch3 = gpio_configure(GPIO_SWITCH3, 1, 0, -1);
	xlog("SWITCH3 is %s", mcp->switch3 ? "ON" : "OFF");

	mcp->switch4 = gpio_configure(GPIO_SWITCH4, 1, 0, -1);
	xlog("SWITCH4 is %s", mcp->switch4 ? "ON" : "OFF");

	mcp->dac_power = gpio_configure(GPIO_DAC_POWER, 1, 0, -1);
	xlog("DAC power is %s", mcp->dac_power ? "ON" : "OFF");

	mcp->ext_power = gpio_configure(GPIO_EXT_POWER, 1, 0, -1);
	xlog("EXT power is %s", mcp->ext_power ? "ON" : "OFF");

	// start dac update thread
	if (pthread_create(&thread, NULL, &dac, NULL))
		return xerr("Error creating thread_dac");

	// prepare the menus
	es9028_prepare_menus();

	xlog("ES9028 initialized");
	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("Error canceling thread_display");

	if (pthread_join(thread, NULL))
		xlog("Error joining thread_display");

	if (i2c > 0)
		close(i2c);
}

MCP_REGISTER(dac_es9028, 3, &init, &stop);
