#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>

#include <sys/ioctl.h>

#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#include "i2c.h"
#include "utils.h"

static pthread_mutex_t lock;

static int _get_shift(uint8_t mask) {
	uint8_t shift = 0;
	if (!mask)
		return 0;

	while (!(mask & 0b00000001)) {
		mask >>= 1;
		shift++;
	}

	return shift;
}

uint8_t i2c_put(int fd, uint8_t addr, uint8_t value) {
	pthread_mutex_lock(&lock);

	if (ioctl(fd, I2C_SLAVE, addr) < 0) {
		pthread_mutex_unlock(&lock);
		return xerr("Error addressing device 0x%02x", addr);
	}

	if (write(fd, &value, 1) != 1) {
		pthread_mutex_unlock(&lock);
		return xerr("Error writing to device");
	}

	pthread_mutex_unlock(&lock);
	return value;
}

uint8_t i2c_get(int fd, uint8_t addr) {
	uint8_t value;

	pthread_mutex_lock(&lock);

	if (ioctl(fd, I2C_SLAVE, addr) < 0) {
		pthread_mutex_unlock(&lock);
		return xerr("Error addressing device 0x%02x", addr);
	}

	if (read(fd, &value, 1) != 1) {
		pthread_mutex_unlock(&lock);
		return xerr("Error reading from device");
	}

	pthread_mutex_unlock(&lock);
	return value;
}

int i2c_read(int fd, uint8_t addr, uint8_t reg, uint8_t *val) {
	__u8 inbuf, outbuf;
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg messages[2];

	/*
	 * In order to read a register, we first do a "dummy write" by writing
	 * 0 bytes to the register we want to read from.  This is similar to
	 * the packet in set_i2c_register, except it's 1 byte rather than 2.
	 */
	outbuf = reg;
	messages[0].addr = addr;
	messages[0].flags = 0;
	messages[0].len = sizeof(outbuf);
	messages[0].buf = &outbuf;

	/* The data will get returned in this structure */
	messages[1].addr = addr;
	messages[1].flags = I2C_M_RD/* | I2C_M_NOSTART*/;
	messages[1].len = sizeof(inbuf);
	messages[1].buf = &inbuf;

	/* Send the request to the kernel and get the result back */
	packets.msgs = messages;
	packets.nmsgs = 2;

	pthread_mutex_lock(&lock);
	if (ioctl(fd, I2C_RDWR, &packets) < 0) {
		pthread_mutex_unlock(&lock);
		return xerr("Error reading data from register 0x%02x", reg);
	}
	pthread_mutex_unlock(&lock);

	*val = inbuf;
	return 0;
}

int i2c_write(int fd, uint8_t addr, uint8_t reg, uint8_t value) {
	__u8 outbuf[2];
	struct i2c_rdwr_ioctl_data packets;
	struct i2c_msg messages[1];

	messages[0].addr = addr;
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

	pthread_mutex_lock(&lock);
	if (ioctl(fd, I2C_RDWR, &packets) < 0) {
		pthread_mutex_unlock(&lock);
		return xerr("Error writing data into register 0x%02x", reg);
	}
	pthread_mutex_unlock(&lock);

	return 0;
}

int i2c_read_bits(int fd, uint8_t addr, uint8_t reg, uint8_t *val, uint8_t mask) {
	// read current
	if (i2c_read(fd, addr, reg, val) < 0)
		return -1;

	// derive shift right from mask
	uint8_t shift = _get_shift(mask);
	*val = (*val & mask) >> shift; // mask and shift bits
	return 0;
}

int i2c_write_bits(int fd, uint8_t addr, uint8_t reg, uint8_t value, uint8_t mask) {
	// read current
	uint8_t current;
	if (i2c_read(fd, addr, reg, &current) < 0)
		return -1;

	// derive shift left from mask
	uint8_t shift = _get_shift(mask);
	current &= ~mask; // clear bits
	current |= (value << shift) & mask; // set bits
	// write
	i2c_write(fd, addr, reg, current);
	return 0;
}

int i2c_set_bit(int fd, uint8_t addr, uint8_t reg, int n) {
	uint8_t value;
	if (i2c_read(fd, addr, reg, &value) < 0)
		return -1;

	value |= 1 << n;
	i2c_write(fd, addr, reg, value);
	return 0;
}

int i2c_clear_bit(int fd, uint8_t addr, uint8_t reg, int n) {
	uint8_t value;
	if (i2c_read(fd, addr, reg, &value) < 0)
		return -1;

	value &= ~(1 << n);
	i2c_write(fd, addr, reg, value);
	return 0;
}

void i2c_dump_reg(int fd, uint8_t addr, uint8_t reg) {
	uint8_t value;
	i2c_read(fd, addr, reg, &value);
	xlog("I2C 0x%02x == 0x%02x 0b%s", reg, value, printbits(value, SPACEMASK));
}
