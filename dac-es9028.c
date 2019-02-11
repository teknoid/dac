#include "dac-es9028.h"

#include <linux/input-event-codes.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <wiringPi.h>

#include "display.h"
#include "display-menu.h"
#include "es9028.h"
#include "i2c.h"
#include "utils.h"

#define msleep(x) usleep(x*1000)

static pthread_t thread_dac;
static void *dac(void *arg);

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
	i2c_read(ADDR, REG_STATUS, &value);
	if (value & 0x01) { // DPLL locked?
		i2c_read(ADDR, REG_SIGNAL, &value);
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
	char value;
	i2c_read(ADDR, REG_SOURCE, &value);
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
	if (i2c_write(ADDR, REG_VOLUME, DEFAULT_VOLUME) < 0) {
		return;
	}
	dac_unmute();

	mcp->dac_power = 1;
	xlog("switched DAC on");

	// power on Externals
	digitalWrite(GPIO_EXT_POWER, 1);
	mcp->ext_power = 1;
	xlog("switched EXT on");
}

static void dac_off() {
	dac_mute();

	// power off Externals and wait to avoid speaker plop
	digitalWrite(GPIO_EXT_POWER, 0);
	mcp->ext_power = 0;
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

	i2c_set_bit(ADDR, REG_FILTER_MUTE, 0);
	mcp->dac_mute = 1;
	xlog("MUTE");
}

void dac_unmute() {
	if (!mcp->dac_power) {
		return;
	}

	i2c_clear_bit(ADDR, REG_FILTER_MUTE, 0);
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
	if (!mcp->dac_power) {
		return;
	}

	switch (source) {
	case mpd:
		i2c_write(ADDR, REG_INPUT, 0x04); // auto detect DSD/PCM
		i2c_write(ADDR, REG_SOURCE, 0x00); // DATA_CLK
		display_fullscreen_char("MPD");
		break;
	case opt:
		i2c_write(ADDR, REG_INPUT, 0x01); // SPDIF
		i2c_write(ADDR, REG_SOURCE, 0x70); // DATA7
		display_fullscreen_char("OPT");
		break;
	case coax:
		i2c_write(ADDR, REG_INPUT, 0x01); // SPDIF
		i2c_write(ADDR, REG_SOURCE, 0x80); // DATA8
		display_fullscreen_char("COX");
		break;
	}
	mcp->dac_source = source;
	mcp->dac_state_changed = 1;
}

int dac_config_get(const void *ptr) {
	const menuconfig_t *config = ptr;
	xlog("dac_config_get");
	return 0;
}

void dac_config_set(const void *ptr, int value) {
	const menuconfig_t *config = ptr;
	xlog("dac_config_set");
}

int dac_init() {
	if (i2c_init(I2C) < 0) {
		return -1;
	}

	pinMode(GPIO_EXT_POWER, OUTPUT);
	pinMode(GPIO_DAC_POWER, OUTPUT);
	pinMode(GPIO_LAMP, OUTPUT);

	mcp->dac_power = digitalRead(GPIO_DAC_POWER);
	xlog("DAC power is %s", mcp->dac_power ? "ON" : "OFF");
	mcp->ext_power = digitalRead(GPIO_EXT_POWER);
	xlog("EXT power is %s", mcp->ext_power ? "ON" : "OFF");

	// start dac update thread
	if (pthread_create(&thread_dac, NULL, &dac, NULL)) {
		xlog("Error creating thread_dac");
		return -1;
	}

	// prepare the menus
	es9028_prepeare_menus();

	xlog("ES9028 initialized");
	return 0;
}

void dac_close() {
	if (pthread_cancel(thread_dac)) {
		xlog("Error canceling thread_display");
	}
	if (pthread_join(thread_dac, NULL)) {
		xlog("Error joining thread_display");
	}
	i2c_close();
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
	case KEY_TIME:
		gpio_toggle(GPIO_LAMP);
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

void *dac(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void *) 0;
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
			xlog("%s DSD %d %03ddB", s, mcp->dac_rate, mcp->dac_volume);
			break;
		case pcm:
			xlog("%s PCM %d/%d %03ddB", s, mcp->mpd_bits, mcp->dac_rate, mcp->dac_volume);
			break;
		case spdif:
			xlog("%s SPDIF %d %03ddB", s, mcp->dac_rate, mcp->dac_volume);
			break;
		case dop:
			xlog("%s DOP %d %03ddB", s, mcp->dac_rate, mcp->dac_volume);
			break;
		default:
			xlog("%s NLOCK", s);
			break;
		}
	}
}
