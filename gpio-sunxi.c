/*
 * simple mmapped gpio bit-banging
 *
 * based on pio.c from
 * https://github.com/linux-sunxi/sunxi-tools
 *
 * (C) Copyright 2022 Heiko Jehmlich <hje@jecons.de>
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
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/types.h>

#include "utils.h"
#include "gpio.h"
#include "mcp.h"

#define	MEM_BASE				0x01c20000

#define PIO_NR_PORTS			9 /* A-I */

#define PIO_OFFSET				0x0800
#define PIO_REG_CFG(B, N, I)	(uint32_t*)(B + PIO_OFFSET + N*0x24 + (I<<2) + 0x00)
#define PIO_REG_DLEVEL(B, N, I)	(uint32_t*)(B + PIO_OFFSET + N*0x24 + (I<<2) + 0x14)
#define PIO_REG_PULL(B, N, I)	(uint32_t*)(B + PIO_OFFSET + N*0x24 + (I<<2) + 0x1C)
#define PIO_REG_DATA(B, N)		(uint32_t*)(B + PIO_OFFSET + N*0x24 + 0x10)

#define TMR_OFFSET				0x0C00
#define TMR_AVSCLK(B)			(uint32_t*)(B + 0x0144)
#define TMR_AVSCTRL(B)			(uint32_t*)(B + TMR_OFFSET + 0x80)
#define TMR_AVS0(B) 			(uint32_t*)(B + TMR_OFFSET + 0x84)
#define TMR_AVS1(B) 			(uint32_t*)(B + TMR_OFFSET + 0x88)
#define TMR_AVSDIV(B)			(uint32_t*)(B + TMR_OFFSET + 0x8c)

#define I2S0_OFFSET				0x2000
#define I2S0_CLKDIV(B)			(uint32_t*)(B + I2S0_OFFSET + 0x24)

typedef struct {
	int port;
	int pin;
	int func;
	int pull;
	int trig;
	int data;
} gpio_status_t;

static volatile void *mem;

#ifdef GPIO_MAIN
static void test_blink() {
	const char *pio = "PA3";

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
}

static void test_timers() {
	uint32_t *avs0 = TMR_AVS0(mem);
	uint32_t *avs1 = TMR_AVS1(mem);
	*avs1 = *avs0 = 0;
	usleep(1000);
	printf("AVS0 %08u AVS1 %08u\n", *avs0, *avs1);
	usleep(1000);
	printf("AVS0 %08u AVS1 %08u\n", *avs0, *avs1);
	usleep(1000);
	printf("AVS0 %08u AVS1 %08u\n", *avs0, *avs1);

	gpio_delay_micros(330);
	printf("AVS1 330 %08u\n", *avs1);
	gpio_delay_micros(188);
	printf("AVS1 188 %08u\n", *avs1);
	gpio_delay_micros(88);
	printf("AVS1 088 %08u\n", *avs1);
	gpio_delay_micros(22);
	printf("AVS1 022 %08u\n", *avs1);

	printf("AVS0 %08u\n", *avs0);
	uint32_t delay = 1813594;
	uint32_t begin = gpio_micros();
	usleep(delay);
	uint32_t elapsed = gpio_micros_since(&begin);
	printf("AVS0 %08u\n", *avs0);
	printf("usleep %u AVS0 elapsed = %u\n", delay, elapsed);
}

static int gpio_main(int argc, char **argv) {
	gpio_init();
	printf("mmap OK\n");

	test_timers();
	test_blink();

	gpio_close();
	return 0;
}
#endif

static void mem_read(gpio_status_t *pio) {
	uint32_t val;
	uint32_t port_num_func, port_num_pull;
	uint32_t offset_func, offset_pull;

	port_num_func = pio->pin >> 3;
	offset_func = (pio->pin & 0x07) << 2;

	port_num_pull = pio->pin >> 4;
	offset_pull = (pio->pin & 0x0f) << 1;

	/* function */
	val = *PIO_REG_CFG(mem, pio->port, port_num_func);
	pio->func = (val >> offset_func) & 0x07;

	/* pull */
	val = *PIO_REG_PULL(mem, pio->port, port_num_pull);
	pio->pull = (val >> offset_pull) & 0x03;

	/* trigger */
	val = *PIO_REG_DLEVEL(mem, pio->port, port_num_pull);
	pio->trig = (val >> offset_pull) & 0x03;

	/* data */
	val = *PIO_REG_DATA(mem, pio->port);
	pio->data = (val >> pio->pin) & 0x01;
}

static void mem_write(gpio_status_t *pio) {
	uint32_t *addr, val;
	uint32_t port_num_func, port_num_pull;
	uint32_t offset_func, offset_pull;

	port_num_func = pio->pin >> 3;
	offset_func = (pio->pin & 0x07) << 2;

	port_num_pull = pio->pin >> 4;
	offset_pull = (pio->pin & 0x0f) << 1;

	/* function */
	addr = PIO_REG_CFG(mem, pio->port, port_num_func);
	val = *addr;
	val &= ~(0x07 << offset_func);
	val |= (pio->func & 0x07) << offset_func;
	*addr = val;

	/* pull */
	addr = PIO_REG_PULL(mem, pio->port, port_num_pull);
	val = *addr;
	val &= ~(0x03 << offset_pull);
	val |= (pio->pull & 0x03) << offset_pull;
	*addr = val;

	/* trigger */
	addr = PIO_REG_DLEVEL(mem, pio->port, port_num_pull);
	val = *addr;
	val &= ~(0x03 << offset_pull);
	val |= (pio->trig & 0x03) << offset_pull;
	*addr = val;

	/* initial value - only if 0/1 - otherwise leave unchanged */
	addr = PIO_REG_DATA(mem, pio->port);
	val = *addr;
	if (pio->data == 0) {
		val &= ~(0x01 << pio->pin);
		*addr = val;
	} else if (pio->data == 1) {
		val |= (0x01 << pio->pin);
		*addr = val;
	} else
		pio->data = (val >> pio->pin) & 0x01;
}

void gpio_x() {
//	uint32_t *addr, val;
//
//	addr = I2S0_CLKDIV(mem);
//	val = *addr;
//	printf("I2S0_CLKDIV 0x%x %s\n", val, printbits(val, SPACEMASK));
//
//	val |= 1 << 9;
//	val = *addr;
//	printf("I2S0_CLKDIV 0x%x %s\n", val, printbits(val, SPACEMASK));
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

	uint32_t val = *PIO_REG_DATA(mem, port);
	return (val >> pin) & 0x01;
}

void gpio_set(const char *name, int value) {
	if (*name == 'P')
		name++;

	int port = *name++ - 'A';
	int pin = atoi(name);

	uint32_t *addr = PIO_REG_DATA(mem, port);
	uint32_t val = *addr;
	if (value)
		val |= (1 << pin);
	else
		val &= ~(1 << pin);
	*addr = val;
}

int gpio_toggle(const char *name) {
	if (*name == 'P')
		name++;

	int port = *name++ - 'A';
	int pin = atoi(name);

	uint32_t *addr = PIO_REG_DATA(mem, port);
	uint32_t val = *addr;
	if ((1 << pin) & val) {
		val &= ~(1 << pin);
		*addr = val;
		return 0;
	} else {
		val |= (1 << pin);
		*addr = val;
		return 1;
	}
}

// usleep() on its own gives latencies 20-40 us; this combination gives < 25 us.
void gpio_delay_micros(uint32_t us) {
	// we use AVS1 Timer
	uint32_t *ctrl = TMR_AVSCTRL(mem);
	uint32_t *avs1 = TMR_AVS1(mem);
	*ctrl &= ~0b10; // stop
	*avs1 = 0; // clear
	*ctrl |= 0b10; // run
	if (us > 100)
		usleep(us - 90);
	while (*avs1 < us)
		;
}

uint32_t gpio_micros() {
	uint32_t *avs0 = TMR_AVS0(mem);
	return *avs0;
}

uint32_t gpio_micros_since(uint32_t *when) {
	uint32_t *avs0 = TMR_AVS0(mem);
	uint32_t now = *avs0;
	uint32_t elapsed;
	if (now > *when)
		elapsed = now - *when;
	else
		elapsed = 0xffffffff - *when + now;
	*when = now;
	return elapsed;
}

static int init() {
	int pagesize = sysconf(_SC_PAGESIZE) * 4;

	// access memory
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd == -1) {
		printf("/dev/mem failed: %s\n", strerror(errno));
		return -2;
	}

	mem = mmap(NULL, pagesize, PROT_WRITE | PROT_READ, MAP_SHARED, fd, MEM_BASE);
	if (mem == MAP_FAILED) {
		printf("mmap gpio failed: %s\n", strerror(errno));
		return -4;
	}

	// enable AVS timers clock, set divisor to 12 --> gives 1us
	uint32_t *avsclk = TMR_AVSCLK(mem);
	uint32_t *avsdiv = TMR_AVSDIV(mem);
	uint32_t *ctrl = TMR_AVSCTRL(mem);
	uint32_t *avs0 = TMR_AVS0(mem);
	uint32_t *avs1 = TMR_AVS1(mem);
	*ctrl &= ~0b11; // stop
	*avs1 = *avs0 = 0;
	*avsdiv = 0x000C000C;
	*avsclk |= 1 << 31;
	*ctrl |= 0b11; // run Forest, run...

	close(fd);
	return 0;
}

static void stop() {
	int pagesize = sysconf(_SC_PAGESIZE) * 4;

	uint32_t *ctrl = TMR_AVSCTRL(mem);
	*ctrl &= ~0b11; // stop AVS timers

	munmap((void*) mem, pagesize);
}

MCP_REGISTER(gpio, 1, &init, &stop);

#ifdef GPIO_MAIN
int main(int argc, char **argv) {
	return gpio_main(argc, argv);
}
#endif
