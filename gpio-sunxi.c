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
#include "gpio.h"

#define PIO_NR_PORTS			9 /* A-I */

#define PIO_REG_CFG(B, N, I)	(uint32_t*)((B) + (N)*0x24 + ((I)<<2) + 0x00)
#define PIO_REG_DLEVEL(B, N, I)	(uint32_t*)((B) + (N)*0x24 + ((I)<<2) + 0x14)
#define PIO_REG_PULL(B, N, I)	(uint32_t*)((B) + (N)*0x24 + ((I)<<2) + 0x1C)
#define PIO_REG_DATA(B, N)		(uint32_t*)((B) + (N)*0x24 + 0x10)

typedef struct {
	int port;
	int pin;
	int func;
	int pull;
	int trig;
	int data;
} gpio_status_t;

static size_t gpio_size;
static volatile char *gpio;

static void mem_read(gpio_status_t *pio) {
	uint32_t val;
	uint32_t port_num_func, port_num_pull;
	uint32_t offset_func, offset_pull;

	port_num_func = pio->pin >> 3;
	offset_func = ((pio->pin & 0x07) << 2);

	port_num_pull = pio->pin >> 4;
	offset_pull = ((pio->pin & 0x0f) << 1);

	/* function */
	val = le32toh(*PIO_REG_CFG(gpio, pio->port, port_num_func));
	pio->func = (val >> offset_func) & 0x07;

	/* pull */
	val = le32toh(*PIO_REG_PULL(gpio, pio->port, port_num_pull));
	pio->pull = (val >> offset_pull) & 0x03;

	/* trigger */
	val = le32toh(*PIO_REG_DLEVEL(gpio, pio->port, port_num_pull));
	pio->trig = (val >> offset_pull) & 0x03;

	/* data */
	val = le32toh(*PIO_REG_DATA(gpio, pio->port));
	pio->data = (val >> pio->pin) & 0x01;
}

static void mem_write(gpio_status_t *pio) {
	uint32_t *addr, val;
	uint32_t port_num_func, port_num_pull;
	uint32_t offset_func, offset_pull;

	port_num_func = pio->pin >> 3;
	offset_func = ((pio->pin & 0x07) << 2);

	port_num_pull = pio->pin >> 4;
	offset_pull = ((pio->pin & 0x0f) << 1);

	/* function */
	addr = PIO_REG_CFG(gpio, pio->port, port_num_func);
	val = le32toh(*addr);
	val &= ~(0x07 << offset_func);
	val |= (pio->func & 0x07) << offset_func;
	*addr = htole32(val);

	/* pull */
	addr = PIO_REG_PULL(gpio, pio->port, port_num_pull);
	val = le32toh(*addr);
	val &= ~(0x03 << offset_pull);
	val |= (pio->pull & 0x03) << offset_pull;
	*addr = htole32(val);

	/* trigger */
	addr = PIO_REG_DLEVEL(gpio, pio->port, port_num_pull);
	val = le32toh(*addr);
	val &= ~(0x03 << offset_pull);
	val |= (pio->trig & 0x03) << offset_pull;
	*addr = htole32(val);

	/* initial value - only if 0/1 - otherwise leave unchanged */
	addr = PIO_REG_DATA(gpio, pio->port);
	val = le32toh(*addr);
	if (pio->data == 0) {
		val &= ~(0x01 << pio->pin);
		*addr = htole32(val);
	} else if (pio->data == 1) {
		val |= (0x01 << pio->pin);
		*addr = htole32(val);
	} else
		pio->data = (int) ((val >> pio->pin) & 0x01);
}

void gpio_print(const char *name) {
	if (*name == 'P')
		name++;

	gpio_status_t pio;
	pio.port = *name++ - 'A';
	pio.pin = atoi(name);
	mem_read(&pio);
	printf("P%c%d", 'A' + pio.port, pio.pin);
	printf("<%x>", pio.func);
	printf("<%x>", pio.pull);
	printf("<%x>", pio.trig);
	printf("<%x>", pio.data);
	printf("\n");
}

int gpio_configure(const char *name, int function, int trigger, int initial) {
	if (*name == 'P')
		name++;

	gpio_status_t pio;
	pio.port = *name++ - 'A';
	pio.pin = atoi(name);
	pio.func = function;
	pio.trig = trigger;
	pio.data = initial;
	mem_write(&pio);
	return pio.data;
}

int gpio_get(const char *name) {
	if (*name == 'P')
		name++;

	int port = *name++ - 'A';
	int pin = atoi(name);

	uint32_t val = le32toh(*PIO_REG_DATA(gpio, port));
	return (int) ((val >> pin) & 0x01);
}

void gpio_set(const char *name, int value) {
	if (*name == 'P')
		name++;

	int port = *name++ - 'A';
	int pin = atoi(name);

	uint32_t *addr = PIO_REG_DATA(gpio, port);
	uint32_t val = le32toh(*addr);
	if (value)
		val |= (0x01 << pin);
	else
		val &= ~(0x01 << pin);
	*addr = htole32(val);
}

int gpio_toggle(const char *name) {
	if (*name == 'P')
		name++;

	int port = *name++ - 'A';
	int pin = atoi(name);

	uint32_t *addr = PIO_REG_DATA(gpio, port);
	uint32_t val = le32toh(*addr);
	if ((1 << pin) & val) {
		val &= ~(1 << pin);
		*addr = htole32(val);
		return 0;
	} else {
		val |= (1 << pin);
		*addr = htole32(val);
		return 1;
	}
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
	const char *pio = "PA3";

	gpio_init();

	gpio_print(pio);

	printf("configure with initial 0\n");
	gpio_configure(pio, 1, 0, 0);
	sleep(1);

	printf("configure with initial 1\n");
	gpio_configure(pio, 1, 0, 1);
	sleep(1);

	printf("configure with initial not set\n");
	gpio_configure(pio, 1, 0, -1);
	sleep(1);

	printf("blink test\n");
	for (int i = 0; i < 3; i++) {
		gpio_set(pio, 1);
		gpio_print(pio);
		sleep(1);
		gpio_set(pio, 0);
		gpio_print(pio);
		sleep(1);
	}
	sleep(1);

	printf("toggle test\n");
	gpio_toggle(pio);
	gpio_print(pio);
	sleep(1);
	gpio_toggle(pio);
	gpio_print(pio);

	gpio_close();
}
#endif
