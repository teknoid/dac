#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include <errno.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include <linux/i2c-dev.h>
#include <linux/input.h>

#include <wiringPi.h>

#include "mcp.h"
#include "utils.h"

#define GPIO_EXT_POWER		0
#define GPIO_DAC_POWER		7

#define GPIO_LAMP			3

#define I2C_DEV				"/dev/i2c-0"
#define I2C_ADDR			0x48

#define REG_VOLUME			0x10
#define REG_STATUS			0x40

#define MCLK				100000000

int fd_i2c;

static int i2c_write(char reg, char value) {
	char outbuf[2];
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg messages[1];

	messages[0].addr = I2C_ADDR;
	messages[0].flags = 0;
	messages[0].len = sizeof(outbuf);
	messages[0].buf = outbuf;

	/* The first byte indicates which register we'll write */
	outbuf[0] = reg;

	/*
	 * The second byte indicates the value to write.  Note that for many
	 * devices, we can write multiple, sequential registers at once by
	 * simply making outbuf bigger.
	 */
	outbuf[1] = value;

	/* Transfer the i2c packets to the kernel and verify it worked */
	packets.msgs = messages;
	packets.nmsgs = 1;
	if (ioctl(fd_i2c, I2C_RDWR, &packets) < 0) {
		xlog("Error writing data into register 0x%02x", reg);
		return -1;
	}
	return 0;
}

static int i2c_read(char reg, char *val) {
	char inbuf, outbuf;
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg messages[2];

	/*
	 * In order to read a register, we first do a "dummy write" by writing
	 * 0 bytes to the register we want to read from.  This is similar to
	 * the packet in set_i2c_register, except it's 1 byte rather than 2.
	 */
	outbuf = reg;
	messages[0].addr = I2C_ADDR;
	messages[0].flags = 0;
	messages[0].len = sizeof(outbuf);
	messages[0].buf = &outbuf;

	/* The data will get returned in this structure */
	messages[1].addr = I2C_ADDR;
	messages[1].flags = I2C_M_RD/* | I2C_M_NOSTART*/;
	messages[1].len = sizeof(inbuf);
	messages[1].buf = &inbuf;

	/* Send the request to the kernel and get the result back */
	packets.msgs = messages;
	packets.nmsgs = 2;
	if (ioctl(fd_i2c, I2C_RDWR, &packets) < 0) {
		xlog("Error reading data from register 0x%02x", reg);
		return -1;
	}
	*val = inbuf;
	return 0;
}

static int i2c_set_bit(char reg, int n) {
	char value;
	if (i2c_read(reg, &value) < 0) {
		return -1;
	}
	value |= 1 << n;
	i2c_write(reg, value);
	return 0;
}

static int i2c_clear_bit(char reg, int n) {
	char value;
	if (i2c_read(reg, &value) < 0) {
		return -1;
	}
	value &= ~(1 << n);
	i2c_write(reg, value);
	return 0;
}

static void i2c_dump_reg(char reg) {
	char value;
	i2c_read(reg, &value);
	xlog("I2C 0x%02x == 0x%02x 0b%s", reg, value, printBits(value));
}

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
	i2c_read(100, &value);
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
	i2c_read(0x42, &v0);
	i2c_read(0x43, &v1);
	i2c_read(0x44, &v2);
	i2c_read(0x45, &v3);

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

	i2c_read(REG_VOLUME, &value);
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
	while (i2c_read(REG_STATUS, &value) < 0) {
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
	if (i2c_write(0x0f, 0x07) < 0) {
		return;
	}
	if (i2c_write(REG_VOLUME, 0x60) < 0) {
		return;
	}
	i2c_dump_reg(REG_STATUS);

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
	i2c_read(REG_VOLUME, &value);
	if (value != 0x00)
		value--;
	if (value != 0x00)
		value--;
	i2c_write(REG_VOLUME, value);
	int db = (value / 2) * -1;
	mcp->dac_volume = db;
	mcp->display_volume_countdown = 10;
	xlog("VOL++ %03d", db);
}

void dac_volume_down() {
	if (!mcp->dac_power) {
		return;
	}

	char value;
	i2c_read(REG_VOLUME, &value);
	if (value != 0xf0)
		value++;
	if (value != 0xf0)
		value++;
	i2c_write(REG_VOLUME, value);
	int db = (value / 2) * -1;
	mcp->dac_volume = db;
	mcp->display_volume_countdown = 10;
	xlog("VOL-- %03d", db);
}

void dac_mute() {
	if (!mcp->dac_power) {
		return;
	}

	i2c_set_bit(0x07, 0);
	mcp->dac_mute = 1;
	xlog("MUTE");
}

void dac_unmute() {
	if (!mcp->dac_power) {
		return;
	}

	i2c_clear_bit(0x07, 0);
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

	display_update();
}

int dac_init() {
	pinMode(GPIO_EXT_POWER, OUTPUT);
	pinMode(GPIO_DAC_POWER, OUTPUT);
	pinMode(GPIO_LAMP, OUTPUT);

	if ((fd_i2c = open(I2C_DEV, O_RDWR)) < 0) {
		xlog("Failed to open the i2c bus");
		return -1;
	}

	mcp->dac_power = digitalRead(GPIO_DAC_POWER);
	if (mcp->dac_power) {
		xlog("DAC  power is ON");
	} else {
		xlog("DAC  power is OFF");
	}

	return 0;
}

void dac_close() {
	if (fd_i2c) {
		close(fd_i2c);
	}
}

void dac_handle(struct input_event ev) {
	switch (ev.code) {
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
	default:
		mpdclient_handle(ev.code);
	}

	display_update();
}
