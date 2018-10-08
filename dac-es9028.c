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

#include "mcp.h"

#define I2C_DEV			"/dev/i2c-0"
#define I2C_ADDR		0x48

#define NLOCK			0
#define PCM				1
#define DSD				2
#define DOP				3

#define MCLK			100000000

int i2c;

static int i2c_write(int file, unsigned char reg, unsigned char value) {
	unsigned char outbuf[2];
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
	if (ioctl(file, I2C_RDWR, &packets) < 0) {
		perror("Unable to send data");
		return 1;
	}
	return 0;
}

static int i2c_read(int file, unsigned char reg, unsigned char *val) {
	unsigned char inbuf, outbuf;
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
	if (ioctl(file, I2C_RDWR, &packets) < 0) {
		perror("Unable to send data");
		return 1;
	}
	*val = inbuf;
	return 0;
}

static int dac_get_signal() {
	unsigned char value;
	i2c_read(i2c, 100, &value);
	if (value == 4) {
		return NLOCK;
	} else if (value == 2) {
		return PCM;
	} else if (value == 1) {
		return DSD;
	}
}

static double dac_get_fsr() {
	unsigned char r66;
	unsigned char r67;
	unsigned char r68;
	unsigned char r69;
	uint32_t dpll;
	uint64_t value;

	i2c_read(i2c, 66, &r66);
	i2c_read(i2c, 67, &r67);
	i2c_read(i2c, 68, &r68);
	i2c_read(i2c, 69, &r69);

	dpll = r69 << 8;
	dpll = dpll << 8 | r68;
	dpll = dpll << 8 | r67;
	dpll = dpll << 8 | r66;

	value = dpll;
	value = (value * MCLK) / 0xffffffff;

	double dvalue = value / 100.0;
	dvalue = round(dvalue) / 10.0;
	return dvalue;
}

static int dac_get_vol() {
	unsigned char value;
	int db;

	i2c_read(i2c, 16, &value);
	db = value / 2;
	return db;
}

void dac_volume_up() {
	unsigned char value;

	i2c_read(i2c, 16, &value);
	// printf("VOLUME 0x%02x \n", value);
	if (value != 0x00)
		value--;
	if (value != 0x00)
		value--;
	i2c_write(i2c, 16, value);
	mcplog("VOL++");
}

void dac_volume_down() {
	unsigned char value;

	i2c_read(i2c, 16, &value);
	// printf("VOLUME 0x%02x \n", value);
	if (value != 0xf0)
		value++;
	if (value != 0xf0)
		value++;
	i2c_write(i2c, 16, value);
	mcplog("VOL--");
}

void dac_select_channel() {
	mcplog("CHANNELUP");
}

int dac_init() {
	if ((i2c = open(I2C_DEV, O_RDWR)) < 0) {
		mcplog("Failed to open the i2c bus");
		return 1;
	}
	return 0;
}

int dac_close() {
	if (i2c) {
		close(i2c);
	}
	return 0;
}

void* dac(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		mcplog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	double fsr;
	int vol, signal;

	while (1) {
		signal = dac_get_signal();
		fsr = dac_get_fsr();
		vol = dac_get_vol();

		switch (signal) {
		case NLOCK:
			printf("NLOCK\n");
		case PCM:
			printf("PCM %.1lf -%03d\n", fsr, vol);
			break;
		case DSD:
			printf("DSD %.1lf -%03d \n", fsr, vol);
			break;
		default:
			printf("??? %.1lf -%03d \n", fsr, vol);
			break;
		}
		usleep(1000 * 1000);
	}
}
