/*
 * based on pio.c from
 * https://github.com/linux-sunxi/sunxi-tools
 *
 * (C) Copyright 2011 Henrik Nordstrom <henrik@henriknordstrom.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "utils.h"

#define PIO_REG_CFG(B, N, I)	((B) + (N)*0x24 + ((I)<<2) + 0x00)
#define PIO_REG_DLEVEL(B, N, I)	((B) + (N)*0x24 + ((I)<<2) + 0x14)
#define PIO_REG_PULL(B, N, I)	((B) + (N)*0x24 + ((I)<<2) + 0x1C)
#define PIO_REG_DATA(B, N)		((B) + (N)*0x24 + 0x10)

#define PIO_NR_PORTS			9 /* A-I */

#define LE32TOH(X)				le32toh(*((uint32_t*)(X)))

static size_t gpio_size;
static volatile char *gpio;

struct pio_status {
	int func;
	int pull;
	int trig;
	int data;
};

static int pio_get(uint32_t port, uint32_t pin, struct pio_status *pio) {
	uint32_t val;
	uint32_t port_num_func, port_num_pull;
	uint32_t offset_func, offset_pull;

	port_num_func = pin >> 3;
	offset_func = ((pin & 0x07) << 2);

	port_num_pull = pin >> 4;
	offset_pull = ((pin & 0x0f) << 1);

	/* function */
	val = LE32TOH(PIO_REG_CFG(gpio, port, port_num_func));
	pio->func = (val >> offset_func) & 0x07;

	/* pull */
	val = LE32TOH(PIO_REG_PULL(gpio, port, port_num_pull));
	pio->pull = (val >> offset_pull) & 0x03;

	/* trigger */
	val = LE32TOH(PIO_REG_DLEVEL(gpio, port, port_num_pull));
	pio->trig = (val >> offset_pull) & 0x03;

	/* data */
	val = LE32TOH(PIO_REG_DATA(gpio, port));
	pio->data = (val >> pin) & 0x01;

	return 1;
}

static int pio_set(uint32_t port, uint32_t pin, struct pio_status *pio) {
	uint32_t *addr, val;
	uint32_t port_num_func, port_num_pull;
	uint32_t offset_func, offset_pull;

	port_num_func = pin >> 3;
	offset_func = ((pin & 0x07) << 2);

	port_num_pull = pin >> 4;
	offset_pull = ((pin & 0x0f) << 1);

	/* function */
	if (pio->func >= 0) {
		addr = (uint32_t*) PIO_REG_CFG(gpio, port, port_num_func);
		val = le32toh(*addr);
		val &= ~(0x07 << offset_func);
		val |= (pio->func & 0x07) << offset_func;
		*addr = htole32(val);
	}

	/* pull */
	if (pio->pull >= 0) {
		addr = (uint32_t*) PIO_REG_PULL(gpio, port, port_num_pull);
		val = le32toh(*addr);
		val &= ~(0x03 << offset_pull);
		val |= (pio->pull & 0x03) << offset_pull;
		*addr = htole32(val);
	}

	/* trigger */
	if (pio->trig >= 0) {
		addr = (uint32_t*) PIO_REG_DLEVEL(gpio, port, port_num_pull);
		val = le32toh(*addr);
		val &= ~(0x03 << offset_pull);
		val |= (pio->trig & 0x03) << offset_pull;
		*addr = htole32(val);
	}

	/* data */
	if (pio->data >= 0) {
		addr = (uint32_t*) PIO_REG_DATA(gpio, port);
		val = le32toh(*addr);
		if (pio->data)
			val |= (0x01 << pin);
		else
			val &= ~(0x01 << pin);
		*addr = htole32(val);
	}

	return 1;
}

void gpio_print(const char *name) {
	if (*name == 'P')
		name++;
	int port = *name++ - 'A';
	int pin = atoi(name);

	struct pio_status pio;
	pio_get(port, pin, &pio);
	printf("P%c%d", 'A' + port, pin);
	printf("<%x>", pio.func);
	printf("<%x>", pio.pull);
	printf("<%x>", pio.trig);
	if (pio.data >= 0)
		printf("<%x>", pio.data);
	fputc('\n', stdout);
}

void gpio_func(const char *name, int function, int trigger) {
	if (*name == 'P')
		name++;
	int port = *name++ - 'A';
	int pin = atoi(name);

	struct pio_status pio;
	pio_get(port, pin, &pio);
	pio.func = function;
	pio.trig = trigger;
	pio_set(port, pin, &pio);
}

void gpio_set(const char *name, int value) {
	if (*name == 'P')
		name++;
	int port = *name++ - 'A';
	int pin = atoi(name);

	struct pio_status pio;
	pio_get(port, pin, &pio);
	pio.func = 1;
	pio.data = value;
	pio_set(port, pin, &pio);
}

int gpio_get(const char *name) {
	if (*name == 'P')
		name++;
	int port = *name++ - 'A';
	int pin = atoi(name);

	struct pio_status pio;
	pio_get(port, pin, &pio);
	return pio.data;
}

void gpio_toggle(const char *name) {
	if (*name == 'P')
		name++;
	int port = *name++ - 'A';
	int pin = atoi(name);

	struct pio_status pio;
	pio_get(port, pin, &pio);
	pio.func = 1;
	if (!pio.data)
		pio.data = 1;
	else
		pio.data = 0;
	pio_set(port, pin, &pio);
}

int gpio_init() {
	int pagesize = sysconf(_SC_PAGESIZE);
	int addr = 0x01c20800 & ~(pagesize - 1);
	int offset = 0x01c20800 & (pagesize - 1);

	int fd = open("/dev/mem", O_RDWR);
	if (fd == -1) {
		perror("open /dev/mem");
		return -1;
	}

	gpio_size = (0x800 + pagesize - 1) & ~(pagesize - 1);
	gpio = mmap(NULL, gpio_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, addr);
	if (!gpio) {
		perror("mmap PIO");
		return -2;
	}

	close(fd);

	gpio += offset;
	return 0;
}

void gpio_close() {
	munmap((void*) gpio, gpio_size);
}

#ifdef GPIO_MAIN
int main(int argc, char **argv) {
	gpio_init();

	gpio_print("PA3");

	for (int i = 0; i < 3; i++) {
		gpio_set("PA3", 1);
		gpio_print("PA3");
		sleep(1);
		gpio_set("PA3", 0);
		gpio_print("PA3");
		sleep(1);
	}

	gpio_close();
}
#endif
