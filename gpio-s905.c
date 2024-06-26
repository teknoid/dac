/*
 * simple mmapped gpio bit-banging
 *
 *   - implementation for Amlogic S905
 *   - tested on Odroid C2, Linux 5.10.102-meson64
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

#include "utils.h"
#include "gpio.h"
#include "mcp.h"

#define PIO_REG_CFG(B, G)		(uint32_t*)(B) + (G/10)
#define PIO_REG_SET(B, G)		(uint32_t*)(B) + (0x1C/4)
#define PIO_REG_CLR(B, G)		(uint32_t*)(B) + (0x28/4)
#define PIO_REG_GET(B, G)		(uint32_t*)(B) + (0x34/4)

typedef struct {
	int pin;
	int func;
	int data;
} gpio_status_t;

static volatile void *gpio;
static volatile uint32_t *timer;

static void test_lirc() {
	const char *pio = "GPIO22";

	gpio_configure(pio, 1, 0, 1);
	gpio_print(pio);
	xlog("3x lirc VOLUP/VOLDOWN\n");
	gpio_lirc(pio, 0x00FD20DF);
	gpio_lirc(pio, 0x00FD20DF);
	gpio_lirc(pio, 0x00FD20DF);
	sleep(1);
	gpio_lirc(pio, 0x00FD10EF);
	gpio_lirc(pio, 0x00FD10EF);
	gpio_lirc(pio, 0x00FD10EF);
	gpio_print(pio);
}

static void test_flamingo() {
	const char *pio = "GPIO17";

	gpio_configure(pio, 1, 0, 0);
	xlog("flamingo v1 ON\n");
	gpio_flamingo_v1(pio, 0x02796056, 28, 4, 330);
	sleep(1);
	xlog("flamingo v2 OFF\n");
	gpio_flamingo_v1(pio, 0x0253174e, 28, 4, 330);
}

static void test_blink() {
	const char *pio = "GPIO17";

	xlog("configure with initial 0\n");
	gpio_configure(pio, 1, 0, 0);
	sleep(1);

	xlog("configure with initial 1\n");
	gpio_configure(pio, 1, 0, 1);
	sleep(1);

	xlog("configure with initial not set\n");
	gpio_configure(pio, 1, 0, -1);
	sleep(1);

	xlog("blink test\n");
	for (int i = 0; i < 3; i++) {
		gpio_set(pio, 1);
		gpio_print(pio);
		sleep(1);
		gpio_set(pio, 0);
		gpio_print(pio);
		sleep(1);
	}
	sleep(1);

	xlog("toggle test\n");
	gpio_toggle(pio);
	gpio_print(pio);
	sleep(1);
	gpio_toggle(pio);
	gpio_print(pio);
}

static void test_timers() {
	uint32_t t;

	xlog("T0 %08u T1 %08u\n", timer[0], timer[1]);
	usleep(1000);
	xlog("T0 %08u T1 %08u\n", timer[0], timer[1]);
	usleep(1000);
	xlog("T0 %08u T1 %08u\n", timer[0], timer[1]);

	t = timer[1];
	gpio_delay_micros(330);
	xlog("T1 330 %08u\n", timer[1] - t);
	t = timer[1];
	gpio_delay_micros(188);
	xlog("T1 188 %08u\n", timer[1] - t);
	t = timer[1];
	gpio_delay_micros(88);
	xlog("T1 088 %08u\n", timer[1] - t);
	t = timer[1];
	gpio_delay_micros(22);
	xlog("T1 022 %08u\n", timer[1] - t);

	uint32_t delay = 1813594;
	uint32_t begin = gpio_micros();
	usleep(delay);
	uint32_t elapsed = gpio_micros_since(&begin);
	xlog("usleep %u T1 elapsed = %u\n", delay, elapsed);
}

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
	xlog("GPIO%d", pio.pin);
	xlog("<%x>", pio.func);
	xlog("<%x>", pio.data);
	xlog("\n");
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

//
// rc1 pattern
//
// short high pulse followed by long low pulse or long high + short low pulse, no clock
//       _              ___
// 0 = _| |___    1 = _|   |_
//
// https://forum.arduino.cc/index.php?topic=201771.0
//
void gpio_flamingo_v1(const char *name, uint32_t message, int bits, int repeat, int pulse) {
	while (*name >= 'A')
		name++;

	int pin = atoi(name);
	uint32_t bit = 1 << pin;
	uint32_t *set = PIO_REG_SET(gpio, pin);
	uint32_t *clr = PIO_REG_CLR(gpio, pin);
	while (repeat--) {
		uint32_t mask = 1 << (bits - 1);

		// sync
		*set |= bit;
		gpio_delay_micros(pulse);
		*clr |= bit;
		gpio_delay_micros(pulse * 15);

		while (mask) {
			if (message & mask) {
				// 1
				*set |= bit;
				gpio_delay_micros(pulse * 3);
				*clr |= bit;
				gpio_delay_micros(pulse);
			} else {
				// 0
				*set |= bit;
				gpio_delay_micros(pulse);
				*clr |= bit;
				gpio_delay_micros(pulse * 3);
			}
			mask = mask >> 1;
		}
	}
	usleep(pulse * 50);
}

//
// rc2 pattern
//
// clock pulse + data pulse, either short or long distance from clock to data pulse
//       _   _               _      _
// 0 = _| |_| |____    1 = _| |____| |_
//
// 32 bit pattern: 00000000 1000001101011010 0001 0001
//                          1                2    3
// 1=Transmitter ID, 16 bit
// 2=Command, 4 bit, 0=OFF, 1=ON
// 3=Channel, 4 bit
//
// https://forum.pilight.org/showthread.php?tid=1110&page=12
void gpio_flamingo_v2(const char *name, uint32_t message, int bits, int repeat, int phi, int plo) {
	while (*name >= 'A')
		name++;

	int px = (phi + plo) * 2 + plo;
	int sync = px * 2;
	int pin = atoi(name);

	uint32_t bit = 1 << pin;
	uint32_t *set = PIO_REG_SET(gpio, pin);
	uint32_t *clr = PIO_REG_CLR(gpio, pin);
	while (repeat--) {
		uint32_t mask = 1 << (bits - 1);

		// sync
		*set |= bit;
		gpio_delay_micros(phi);
		*clr |= bit;
		gpio_delay_micros(sync);

		while (mask) {
			if (message & mask) {
				// 1
				*set |= bit;
				gpio_delay_micros(phi);
				*clr |= bit;
				gpio_delay_micros(px);
				*set |= bit;
				gpio_delay_micros(phi);
				*clr |= bit;
				gpio_delay_micros(plo);
			} else {
				// 0
				*set |= bit;
				gpio_delay_micros(phi);
				*clr |= bit;
				gpio_delay_micros(plo);
				*set |= bit;
				gpio_delay_micros(phi);
				*clr |= bit;
				gpio_delay_micros(px);
			}

			mask = mask >> 1;
		}

		// a clock or parity (?) bit terminates the message
		*set |= bit;
		gpio_delay_micros(phi);
		*clr |= bit;
		gpio_delay_micros(plo);

		// wait before sending next sync
		gpio_delay_micros(sync * 4);
	}
	usleep(sync * 50);
}

void gpio_lirc(const char *name, uint32_t message) {
}

void gpio_delay_micros(uint32_t us) {
	// usleep() on its own gives latencies 20-40 us; this combination gives < 25 us.
	uint32_t start = timer[1];
	if (us >= 100)
		usleep(us - 80);
	while ((timer[1] - start) < us)
		;
}

uint32_t gpio_micros() {
	return timer[1];
}

uint32_t gpio_micros_since(uint32_t *when) {
	uint32_t now = timer[1];
	uint32_t elapsed;
	if (now > *when)
		elapsed = now - *when;
	else
		elapsed = UINT32_MAX - *when + now;
	*when = now;
	return elapsed;
}

static int init() {
	int pagesize = sysconf(_SC_PAGESIZE);

	if (timer != 0 && gpio != 0)
		return 0; // already initalized

	// figure out the IO Base Address
	FILE *f = fopen("/proc/cpuinfo", "r");
	char buf[1024];
	fgets(buf, sizeof(buf), f); // skip first line
	fgets(buf, sizeof(buf), f); // model name
	uint32_t base = 0;
	if (strstr(buf, "ARMv6"))
		base = 0x20000000;
	else if (strstr(buf, "ARMv7"))
		base = 0x3f000000;
	else if (strstr(buf, "ARMv8"))
		base = 0x3f000000;
	else
		return xerr("Unknown CPU type\n");
	fclose(f);

	// access memory
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (fd == -1)
		return xerr("/dev/mem failed: %s\n", strerror(errno));

	// mmap timer
	timer = (uint32_t*) mmap(NULL, pagesize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, base + 0x3000);
	if (timer == MAP_FAILED)
		return xerr("mmap timer failed: %s\n", strerror(errno));

	// mmap gpio
	gpio = mmap(NULL, pagesize, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, base + 0x200000);
	if (gpio == MAP_FAILED)
		return xerr("mmap gpio failed: %s\n", strerror(errno));

	close(fd);
	return 0;
}

static void stop() {
	int pagesize = sysconf(_SC_PAGESIZE);
	munmap((void*) gpio, pagesize);
	munmap((void*) timer, pagesize);
}

int gpio_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	init();
	xlog("mmap OK\n");

	test_timers();
	test_lirc();
	test_flamingo();
	test_blink();

	stop();
	return 0;
}

#ifdef GPIO_MAIN
int main(int argc, char **argv) {
	return gpio_main(argc, argv);
}
#else
MCP_REGISTER(gpio, 1, &init, &stop, NULL);
#endif
