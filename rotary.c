#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <wiringPi.h>

#include "mcp.h"

struct encoder {
	int pin_a;
	int pin_b;
	int pin_s;
	volatile long value;
	volatile int lastEncoded;
	volatile int button;
};

struct encoder *encoder;

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

static struct encoder *setupencoder(int pin_a, int pin_b, int pin_s) {
	encoder = malloc(sizeof(*encoder));

	encoder->pin_a = pin_a;
	encoder->pin_b = pin_b;
	encoder->pin_s = pin_s;
	encoder->value = 0;
	encoder->lastEncoded = 0;
	encoder->button = 1;

	pinMode(pin_a, INPUT);
	pinMode(pin_b, INPUT);
	pinMode(pin_s, INPUT);
	pullUpDnControl(pin_a, PUD_UP);
	pullUpDnControl(pin_b, PUD_UP);
	pullUpDnControl(pin_s, PUD_UP);
	wiringPiISR(pin_a, INT_EDGE_BOTH, updateEncoders);
	wiringPiISR(pin_b, INT_EDGE_BOTH, updateEncoders);
	wiringPiISR(pin_s, INT_EDGE_BOTH, updateEncoders);

	return encoder;
}

void* rotary(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		mcplog("Error setting pthread_setcancelstate");
		return (void *) 0;
	}

	long old = 0;
	while (1) {
		long new = encoder->value;
		if (new < old) {
			dac_volume_down();
		} else if (new > old) {
			dac_volume_up();
		} else if (encoder->button == 0) {
			dac_select_channel();
			usleep(300 * 1000);
		}
		old = new;
		usleep(100 * 1000);
	}
}

int rotary_init() {
	encoder = setupencoder(GPIO_ENC_A, GPIO_ENC_B, GPIO_SWITCH);
	return 0;
}
