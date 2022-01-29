/*
 * simple mmapped gpio bit-banging
 *
 * based on
 * https://elinux.org/RPi_GPIO_Code_Samples
 *
 * (C) Copyright 2022 Heiko Jehmlich <hje@jecons.de>
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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>

// for the Pi 1
// #define MEMBASE				0x20200000
// for the Pi 2 & 3
#define MEMBASE					0x3f200000

#define PIO_REG_CFG(B, G)		(uint32_t*)(B) + (G/10)
#define PIO_REG_SET(B, G)		(uint32_t*)(B) + 0x1C/4
#define PIO_REG_CLR(B, G)		(uint32_t*)(B) + 0x28/4
#define PIO_REG_GET(B, G)		(uint32_t*)(B) + 0x34/4

typedef struct {
	int pin;
	int func;
	int data;
} gpio_status_t;

static volatile void *gpio;

static void mem_read(gpio_status_t *pio) {
	uint32_t *addr;
	int offset_func = (pio->pin % 10) * 3;

	/* function */
	addr = PIO_REG_CFG(gpio, pio->pin);
	pio->func = (*addr >> offset_func) & 0x07;

	/* data */
	addr = PIO_REG_GET(gpio, pio->pin);
	pio->data = (*addr >> pio->pin) & 0x01;
}

static void mem_write(gpio_status_t *pio) {
	uint32_t *addr, val;
	int offset_func = (pio->pin % 10) * 3;
	uint32_t bit = 1 << pio->pin;

	/* function */
	addr = PIO_REG_CFG(gpio, pio->pin);
	val = *addr;
	val &= ~(0x07 << offset_func);
	val |= (pio->func & 0x07) << offset_func;
	*addr = val;

	/* initial value - only if 0/1 - otherwise leave unchanged */
	if (pio->data == 0) {
		addr = PIO_REG_CLR(gpio, pin);
		val = *addr;
		val |= bit;
		*addr = val;
	} else if (pio->data == 1) {
		addr = PIO_REG_SET(gpio, pin);
		val = *addr;
		val |= bit;
		*addr = val;
	} else {
		addr = PIO_REG_GET(gpio, pio->pin);
		val = *addr;
		pio->data = (val >> pio->pin) & 0x01;
	}
}

void gpio_print(const char *name) {
	while (*name >= 'A')
		name++;

	gpio_status_t pio;
	pio.pin = atoi(name);
	mem_read(&pio);
	printf("GPIO%d", pio.pin);
	printf("<%x>", pio.func);
	printf("<%x>", pio.data);
	printf("\n");
}

int gpio_configure(const char *name, int function, int trigger, int initial) {
	while (*name >= 'A')
		name++;

	gpio_status_t pio;
	pio.pin = atoi(name);
	pio.func = function;
	pio.data = initial;
	mem_write(&pio);
	return pio.data;
}

int gpio_get(const char *name) {
	while (*name >= 'A')
		name++;

	int pin = atoi(name);
	uint32_t *addr = PIO_REG_GET(gpio, pio->pin);
	return (*addr >> pin) & 0x01;
}

void gpio_set(const char *name, int value) {
	while (*name >= 'A')
		name++;

	uint32_t bit = 1 << atoi(name);
	uint32_t *addr, val;
	if (value) {
		addr = PIO_REG_SET(gpio, pin);
		val = *addr;
		val |= bit;
		*addr = val;
	} else {
		addr = PIO_REG_CLR(gpio, pin);
		val = *addr;
		val |= bit;
		*addr = val;
	}
}

int gpio_toggle(const char *name) {
	while (*name >= 'A')
		name++;

	uint32_t bit = 1 << atoi(name);
	uint32_t *addr, val;
	addr = PIO_REG_GET(gpio, pin);
	if (*addr & bit) {
		addr = PIO_REG_CLR(gpio, pin);
		val = *addr;
		val |= bit;
		*addr = val;
		return 0;
	} else {
		addr = PIO_REG_SET(gpio, pin);
		val = *addr;
		val |= bit;
		*addr = val;
		return 1;
	}
}

int gpio_init() {
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd == -1) {
		printf("/dev/mem failed: %s\n", strerror(errno));
		return -1;
	}

	gpio = mmap(NULL, 4 * 1024, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, 0x3f200000);
	close(fd);

	if (gpio == MAP_FAILED) {
		printf("mmap failed: %s\n", strerror(errno));
		return -2;
	} else
		return 0;
}

void gpio_close() {
	munmap((void*) gpio, 4 * 1024);
}

#ifdef GPIO_MAIN
int main(int argc, char **argv) {
	const char *pio = "GPIO17";

	gpio_init();
	printf("mmap OK\n");

	gpio_print(pio);
	printf("print OK\n");

	printf("configure with initial 0\n");
	gpio_configure(pio, 1, 0, 0);
	printf("OK\n");
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
