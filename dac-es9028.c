#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include <errno.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
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

#define GPIO_EXT_POWER		0
#define GPIO_DAC_POWER		7

#define GPIO_LAMP			3

#define I2C_DEV				"/dev/i2c-0"
#define I2C_ADDR			0x48

#define REG_VOLUME			0x10
#define REG_STATUS			0x40

#define NLOCK				0
#define PCM					1
#define DSD					2
#define DOP					3

#define MCLK				100000000

int i2c;

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
	if (ioctl(i2c, I2C_RDWR, &packets) < 0) {
		mcplog("Error writing data into register 0x%02x", reg);
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
	if (ioctl(i2c, I2C_RDWR, &packets) < 0) {
		mcplog("Error reading data from register 0x%02x", reg);
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
	mcplog("i2c register 0x%02x 0x%02x %s", reg, value, printBits(value));
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

static int dac_get_signal() {
	char value;
	i2c_read(100, &value);
	if (value == 2) {
		return PCM;
	} else if (value == 1) {
		return DSD;
	} else {
		return NLOCK;
	}
}

static double dac_get_fsr() {
	double dvalue;
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

	dvalue = value / 100.0;
	dvalue = round(dvalue) / 10.0;
	return dvalue;
}

static int dac_get_vol() {
	char value;
	int db;

	i2c_read(REG_VOLUME, &value);
	db = value / 2;
	return db;
}

void dac_volume_up() {
	char value;
	int db;

	i2c_read(REG_VOLUME, &value);
	if (value != 0x00)
		value--;
	if (value != 0x00)
		value--;
	i2c_write(REG_VOLUME, value);
	db = value / 2;
	mcplog("VOL++ -%03d", db);
}

void dac_volume_down() {
	char value;
	int db;

	i2c_read(REG_VOLUME, &value);
	if (value != 0xf0)
		value++;
	if (value != 0xf0)
		value++;
	i2c_write(REG_VOLUME, value);
	db = value / 2;
	mcplog("VOL-- -%03d", db);
}

void dac_mute() {
	i2c_set_bit(0x07, 0);
	mcplog("MUTE");
}

void dac_unmute() {
	i2c_clear_bit(0x07, 0);
	mcplog("UNMUTE");
}

void dac_on() {
	char value;

	// power on
	digitalWrite(GPIO_DAC_POWER, 1);
	mcplog("switched DAC on");
	msleep(100);

	// check status
	int timeout = 10;
	while (i2c_read(REG_STATUS, &value) < 0) {
		msleep(100);
		if (--timeout == 0) {
			mcplog("no answer, aborting.");
			return;
		}
		mcplog("waiting for DAC status %d", timeout);
	}
	value >>= 2;
	if (value == 0b101010) {
		mcplog("Found DAC ES9038Pro");
	} else if (value == 0b011100) {
		mcplog("Found DAC ES9038Q2M");
	} else if (value == 0b101001 || value == 0b101000) {
		mcplog("Found DAC ES9028Pro");
	}

	// initialize registers
	dac_mute();
	if (i2c_write(0x0f, 0x07) < 0) {
		return;
	}
	if (i2c_write(REG_VOLUME, 0x60) < 0) {
		return;
	}
	dac_unmute();

	// power on Externals
	digitalWrite(GPIO_EXT_POWER, 1);
	mcplog("switched EXT on");
}

void dac_off() {
	dac_mute();

	// power off Externals and wait to avoid speaker plop
	digitalWrite(GPIO_EXT_POWER, 0);
	mcplog("switched EXT off");
	sleep(6);

	// power off DAC
	digitalWrite(GPIO_DAC_POWER, 0);
	mcplog("switched DAC off");
}

void dac_update() {
	sleep(1);

	double fsr = dac_get_fsr();
	int signal = dac_get_signal();
	int vol = dac_get_vol();

	switch (signal) {
	case NLOCK:
		mcplog("NLOCK");
	case PCM:
		mcplog("PCM %.1lf -%03d", fsr, vol);
		break;
	case DSD:
		mcplog("DSD %.1lf -%03d", fsr, vol);
		break;
	default:
		mcplog("??? %.1lf -%03d", fsr, vol);
		break;
	}
}

int dac_init() {
	pinMode(GPIO_EXT_POWER, OUTPUT);
	pinMode(GPIO_DAC_POWER, OUTPUT);
	pinMode(GPIO_LAMP, OUTPUT);

	if ((i2c = open(I2C_DEV, O_RDWR)) < 0) {
		mcplog("Failed to open the i2c bus");
		return 1;
	}

	int pin = digitalRead(GPIO_DAC_POWER);
	if (pin == 1) {
		power_state = on;
		mcplog("entered power state ON");
		dac_update();
	} else {
		power_state = stdby;
		mcplog("entered power state STDBY");
	}

	return 0;
}

void dac_close() {
	if (i2c) {
		close(i2c);
	}
}

void dac_handle(int key) {
	switch (key) {
	case KEY_TIME:
		gpio_toggle(GPIO_LAMP);
		break;
	}
}

void *dac(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		mcplog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

//	msleep(500);
//	i2c_dump(REG_STATUS);

//	while (1) {
//		i2c_dump(REG_STATUS);
//		sleep(5);
//	}
	return (void *) 0;
}
