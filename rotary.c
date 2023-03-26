#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

#ifdef WIRINGPI
#include <wiringPi.h>
#endif

#include <linux/input.h>

#include "mcp.h"
#include "utils.h"

#ifndef GPIO_ENC_A
#define GPIO_ENC_A	0
#endif

#ifndef GPIO_ENC_B
#define GPIO_ENC_B	0
#endif

#ifndef GPIO_SWITCH
#define GPIO_SWITCH	0
#endif

struct encoder {
	int pin_a;
	int pin_b;
	int pin_s;
	volatile long value;
	volatile int lastEncoded;
	volatile int button;
};

static pthread_t thread;
static struct encoder *encoder;

#ifdef WIRINGPI
static void updateEncoders() {
	int MSB = digitalRead(encoder->pin_a);
	int LSB = digitalRead(encoder->pin_b);
	int encoded = (MSB << 1) | LSB;
	int sum = (encoder->lastEncoded << 2) | encoded;

	if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011)
		encoder->value++;
	if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000)
		encoder->value--;

	encoder->lastEncoded = encoded;
	encoder->button = digitalRead(encoder->pin_s);
}
#endif

static struct encoder* setupencoder(int pin_a, int pin_b, int pin_s) {
	encoder = malloc(sizeof(*encoder));

	encoder->pin_a = pin_a;
	encoder->pin_b = pin_b;
	encoder->pin_s = pin_s;
	encoder->value = 0;
	encoder->lastEncoded = 0;
	encoder->button = 1;

#ifdef WIRINGPI
	pinMode(pin_a, INPUT);
	pinMode(pin_b, INPUT);
	pinMode(pin_s, INPUT);
	pullUpDnControl(pin_a, PUD_UP);
	pullUpDnControl(pin_b, PUD_UP);
	pullUpDnControl(pin_s, PUD_UP);
	wiringPiISR(pin_a, INT_EDGE_BOTH, updateEncoders);
	wiringPiISR(pin_b, INT_EDGE_BOTH, updateEncoders);
	wiringPiISR(pin_s, INT_EDGE_BOTH, updateEncoders);
#endif

	return encoder;
}

static void* rotary(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	long old = 0;

	struct input_event ev;
	ev.type = EV_KEY;
	ev.value = 1;
	ev.time.tv_sec = 0;
	ev.time.tv_usec = 0;
	while (1) {
		long new = encoder->value;
		if (new < old) {
			ev.code = KEY_KPPLUS;
		} else if (new > old) {
			ev.code = KEY_KPMINUS;
		} else if (encoder->button == 0) {
			ev.code = KEY_ENTER;
		}
		old = new;
		dac_handle(ev.code);
		usleep(100 * 1000);
	}
}

static int init() {
	encoder = setupencoder(GPIO_ENC_A, GPIO_ENC_B, GPIO_SWITCH);

	if (pthread_create(&thread, NULL, &rotary, NULL))
		return xerr("Error creating thread_rotary");

	return 0;
}

static void destroy() {
	if (pthread_cancel(thread))
		xlog("Error canceling thread_rotary");

	if (pthread_join(thread, NULL))
		xlog("Error joining thread_rotary");
}

MCP_REGISTER(rotary, 1, &init, &destroy);

