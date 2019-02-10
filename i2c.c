#include "i2c.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>

// !!! do not include when compiling on ARM board
// #include <linux/i2c.h>

#include "utils.h"

static int fd_i2c;

int i2c_read(uint8_t addr, uint8_t reg, char *val) {
	char inbuf, outbuf;
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
	if (ioctl(fd_i2c, I2C_RDWR, &packets) < 0) {
		xlog("Error reading data from register 0x%02x", reg);
		return -1;
	}
	*val = inbuf;
	return 0;
}

int i2c_write(uint8_t addr, uint8_t reg, char value) {
	char outbuf[2];
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
	if (ioctl(fd_i2c, I2C_RDWR, &packets) < 0) {
		xlog("Error writing data into register 0x%02x", reg);
		return -1;
	}
	return 0;
}

// TODO test
int i2c_write_bits(uint8_t addr, uint8_t reg, char value, uint8_t mask) {
	// derive left shift from mask
	uint8_t shift = 0, temp = mask;
	while (!(temp >> 1)) {
		shift++;
	}
	// read current
	char current;
	if (i2c_read(addr, reg, &current) < 0) {
		return -1;
	}
	current &= ~mask; // clear bits
	current |= (value << shift) & mask; // set bits
	// write
	i2c_write(addr, reg, value);
	return 0;
}

int i2c_set_bit(uint8_t addr, uint8_t reg, int n) {
	char value;
	if (i2c_read(addr, reg, &value) < 0) {
		return -1;
	}
	value |= 1 << n;
	i2c_write(addr, reg, value);
	return 0;
}

int i2c_clear_bit(uint8_t addr, uint8_t reg, int n) {
	char value;
	if (i2c_read(addr, reg, &value) < 0) {
		return -1;
	}
	value &= ~(1 << n);
	i2c_write(addr, reg, value);
	return 0;
}

void i2c_dump_reg(uint8_t addr, uint8_t reg) {
	char value;
	i2c_read(addr, reg, &value);
	xlog("I2C 0x%02x == 0x%02x 0b%s", reg, value, printBits(value));
}

int i2c_init(char *device) {
	if ((fd_i2c = open(device, O_RDWR)) < 0) {
		xlog("Failed to open the i2c bus");
		return -1;
	}

	return 0;
}

void i2c_close() {
	if (fd_i2c) {
		close(fd_i2c);
	}
}
