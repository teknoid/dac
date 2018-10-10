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

#include <wiringPi.h>

#include "mcp.h"

#define GPIO_EXT_POWER		7
#define GPIO_DAC_POWER		16
#define GPIO_DAC_RESET		15

#define I2C_DEV				"/dev/i2c-0"
#define I2C_ADDR			0x48

#define REG_VOLUME1			0x0f
#define REG_VOLUME2			0x10
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
		mcplog("Unable to send data");
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
		mcplog("Unable to send data");
		return -1;
	}
	*val = inbuf;
	return 0;
}

static void debug(char reg, char value) {
	char *bits = printBits(value);
	mcplog("i2c register 0x%02x 0x%02x %s", reg, value, bits);
}

static void i2c_debug(char reg) {
	char value;
	i2c_read(reg, &value);
	debug(reg, value);
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
	char r66;
	char r67;
	char r68;
	char r69;
	uint32_t dpll;
	uint64_t value;

	i2c_read(66, &r66);
	i2c_read(67, &r67);
	i2c_read(68, &r68);
	i2c_read(69, &r69);

	dpll = r69 << 8;
	dpll = dpll << 8 | r68;
	dpll = dpll << 8 | r67;
	dpll = dpll << 8 | r66;

	value = dpll;
	value = (value * MCLK) / 0xffffffff;

	dvalue = value / 100.0;
	dvalue = round(dvalue) / 10.0;
	return dvalue;
}

static int dac_get_vol() {
	char value;
	int db;

	i2c_read(REG_VOLUME2, &value);
	db = value / 2;
	return db;
}

void dac_volume_up() {
	char value;
	int db;

	i2c_read(REG_VOLUME2, &value);
	if (value != 0x00)
		value--;
	if (value != 0x00)
		value--;
	i2c_write(REG_VOLUME2, value);
	db = value / 2;
	mcplog("VOL++ -%03d", db);
}

void dac_volume_down() {
	char value;
	int db;

	i2c_read(REG_VOLUME2, &value);
	if (value != 0xf0)
		value++;
	if (value != 0xf0)
		value++;
	i2c_write(REG_VOLUME2, value);
	db = value / 2;
	mcplog("VOL-- -%03d", db);
}

void dac_select_channel() {
	mcplog("CHANNELUP");
}

void dac_on() {
	char value;

	// power on DAC
	digitalWrite(GPIO_DAC_POWER, 1);
	mcplog("switched DAC on");

	// start DAC
	digitalWrite(GPIO_DAC_RESET, 1);
	msleep(200);
	if (i2c_read(REG_STATUS, &value) < 0) {
		mcplog("I2C error, aborting.");
		return;
	}

	// initialize DAC registers
	msleep(100);
	i2c_write(REG_VOLUME1, 0x07);
	msleep(100);
	i2c_write(REG_VOLUME2, 0x60);

	// power on Externals
	digitalWrite(GPIO_EXT_POWER, 1);
	mcplog("switched EXT on");
}

void dac_off() {
	// power off Externals
	digitalWrite(GPIO_EXT_POWER, 0);
	mcplog("switched EXT off");
	sleep(1);

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
	pinMode(GPIO_DAC_RESET, OUTPUT);

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

void *dac(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		mcplog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	i2c_debug(REG_STATUS);

	//	while (1) {
//		i2c_debug(REG_STATUS);
//		sleep(5);
//	}
	return (void *) 0;
}
