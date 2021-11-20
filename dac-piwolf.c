#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>

#include <wiringPi.h>

#include "mcp.h"
#include "utils.h"

#define GPIO_POWER		0
#define GPIO_LIRC_TX	3

#define TH1				9020
#define	TH2				4460

#define T1				580
#define T01				1660
#define T00				550

#define KEY_VUP			0x00FD20DF
#define KEY_VDOWN		0x00FD10EF
#define KEY_CUP			0x00FD609F
#define KEY_CDOWN		0x00FD50AF

static volatile unsigned long *systReg = 0;

static int init_micros() {
	// based on pigpio source; simplified and re-arranged
	int fdMem = open("/dev/mem", O_RDWR | O_SYNC);
	if (fdMem < 0) {
		perror("Cannot map memory (need sudo?)\n");
		return -1;
	}
	// figure out the address
	FILE *f = fopen("/proc/cpuinfo", "r");
	char buf[1024];
	fgets(buf, sizeof(buf), f); // skip first line
	fgets(buf, sizeof(buf), f); // model name
	unsigned long phys = 0;
	if (strstr(buf, "ARMv6")) {
		phys = 0x20000000;
	} else if (strstr(buf, "ARMv7")) {
		phys = 0x3F000000;
	} else if (strstr(buf, "ARMv8")) {
		phys = 0x3F000000;
	} else {
		perror("Unknown CPU type\n");
		return -1;
	}
	fclose(f);
	systReg = (unsigned long*) mmap(0, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fdMem, phys + 0x3000);
	return 0;
}

static void delay_micros(unsigned int us) {
	// usleep() on its own gives latencies 20-40 us; this combination gives < 25 us.
	unsigned long start = systReg[1];
	if (us >= 100)
		usleep(us - 50);
	while ((systReg[1] - start) < us)
		;
}

static void lirc_send(unsigned long m) {
	unsigned long mask = 1 << 31;

	// sync
	digitalWrite(GPIO_LIRC_TX, 0);
	delay_micros(TH1);
	digitalWrite(GPIO_LIRC_TX, 1);
	delay_micros(TH2);

	while (mask) {
		digitalWrite(GPIO_LIRC_TX, 0);
		delay_micros(T1);
		digitalWrite(GPIO_LIRC_TX, 1);

		if (m & mask)
			delay_micros(T01); // 1
		else
			delay_micros(T00); // 0

		mask = mask >> 1;
	}
	digitalWrite(GPIO_LIRC_TX, 0);
	delay_micros(T1);
	digitalWrite(GPIO_LIRC_TX, 1);

	usleep(100000);
}

// WM8741 workaround: switch through all channels
static void workaround_channel() {
	lirc_send(KEY_CUP);
	lirc_send(KEY_CUP);
	lirc_send(KEY_CUP);
	lirc_send(KEY_CUP);
	lirc_send(KEY_VDOWN);
	lirc_send(KEY_VUP);
	xlog("WM8741 workaround channel");
}

// WM8741 workaround: touch volume
static void workaround_volume() {
	dac_volume_down();
	dac_volume_up();
	xlog("WM8741 workaround volume");
}

static void dac_on() {
	digitalWrite(GPIO_POWER, 1);
	mcp->dac_power = 1;
	xlog("switched DAC on");
	sleep(3);
	workaround_volume();
}

static void dac_off() {
	digitalWrite(GPIO_POWER, 0);
	mcp->dac_power = 0;
	xlog("switched DAC off");
}

void dac_power() {
	if (!mcp->dac_power) {
		dac_on();
		mpdclient_handle(KEY_PLAY);
	} else {
		mpdclient_handle(KEY_STOP);
		dac_off();
	}
}

void dac_volume_up() {
	xlog("VOL++");
	lirc_send(KEY_VUP);
}

void dac_volume_down() {
	xlog("VOL--");
	lirc_send(KEY_VDOWN);
}

void dac_mute() {
}

void dac_unmute() {
}

void dac_source(int source) {
}

int dac_status_get(const void *p1, const void *p2) {
	return 0;
}

void dac_status_set(const void *p1, const void *p2, int value) {
}

int dac_init() {
	init_micros();

	pinMode(GPIO_POWER, OUTPUT);

	pinMode(GPIO_LIRC_TX, OUTPUT);
	digitalWrite(GPIO_LIRC_TX, 1);

	mcp->dac_power = digitalRead(GPIO_POWER);
	if (mcp->dac_power) {
		xlog("DAC  power is ON");
	} else {
		xlog("DAC  power is OFF");
	}

	return 0;
}

void dac_close() {
}

void dac_handle(int c) {
	switch (c) {
	case KEY_VOLUMEUP:
		dac_volume_up();
		break;
	case KEY_VOLUMEDOWN:
		dac_volume_down();
		break;
	case KEY_PAUSE:
	case KEY_PLAY:
		workaround_volume();
		mpdclient_handle(c);
		break;
	case KEY_EJECTCD:
		workaround_channel();
		break;
	case KEY_SELECT:
		lirc_send(KEY_CUP);
		xlog("CHANNELUP");
		break;
	case KEY_POWER:
		dac_power();
		break;
	default:
		mpdclient_handle(c);
	}
}
