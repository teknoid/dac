#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include <sys/socket.h>
#include <arpa/inet.h>

#include "ledstrip.h"
#include "utils.h"
#include "curl.h"
#include "mcp.h"

// gcc -DLEDSTRIP_MAIN -I ./include/ -o ledstrip ledstrip.c utils.c curl.c -lcurl

#define STRIP			"192.168.25.227"
#define DELAY_FADE		3000
#define DELAY_BLINK		500
#define SINGLE			1

#define RED				1
#define GREEN			2
#define BLUE			3

#define	YMIN			10
#define YMAX			50

// Photometric/digital ITU BT.709
#define RR				.2126
#define GG				.7152
#define BB				.0722

// Digital ITU BT.601 (gives more weight to the R and B components)
// makes no sense on a cyan optimized LED Strip
// see https://electronics.stackexchange.com/questions/437871/why-cant-rgb-or-bicolour-leds-produce-a-decent-yellow
//#define RR  			.299
//#define GG  			.587
//#define BB  			.114

enum emode {
	None, Color, Fade, Blink
};

static int sock = 0;
static struct sockaddr_in sock_addr_in = { 0 };
static struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

static uint8_t power = 0, old_power = 0, mode = 0, old_mode = 0, blink_color = 0, r, g, b;

// echo -n "410B0001000000000004FF000000" |xxd -r -p |nc -4u -w0 licht-decke 4048
// echo -n "410B000100000000000400FF0000" |xxd -r -p |nc -4u -w0 licht-decke 4048
// echo -n "410B00010000000000040000FF00" |xxd -r -p |nc -4u -w0 licht-decke 4048
static int ddp() {
	unsigned char msg[] = { 0x0b, 0x41, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00 };

	msg[10] = r;
	msg[11] = g;
	msg[12] = b;
	// hexdump(0, msg, sizeof(msg));

	if (sendto(sock, msg, sizeof(msg), 0, sa, sizeof(*sa)) < 0)
		return xerr("LEDSTRIP Error sending UDP message");

//	int y = RR * r + GG * g + BB * b;
//	xlog("LEDSTRIP rgb %02x%02x%02x y=%d", r, g, b, y);

	return 0;
}

static void blink_loop() {
	xdebug("LEDSTRIP running blink_loop()");

	// store colors
	uint8_t rr = r, gg = g, bb = b;
	for (int i = 0; i < 3; i++) {
		if (!power || mode != Blink)
			return;

		r = blink_color == RED ? 0xff : 0;
		g = blink_color == GREEN ? 0xff : 0;
		b = blink_color == BLUE ? 0xff : 0;
		ddp();
		msleep(DELAY_BLINK);
		r = g = b = 0;
		ddp();
		msleep(DELAY_BLINK);
	}

	// restore state and colors
	if (!old_power)
		ledstrip_off();
	mode = old_mode;
	r = rr;
	g = gg;
	b = bb;
}

static void fade_loop() {
	xdebug("LEDSTRIP running fade_loop()");

	uint8_t rr = r ? r : 0x55;
	uint8_t gg = g ? g : 0x55;
	uint8_t bb = b ? b : 0x55;

	r = rr;
	g = gg;
	b = bb;
	ddp();

	while (1) {
		if (!power || mode != Fade)
			return;

		if (r == rr && g == gg && b == bb) {
			xlog("LEDSTRIP target color reached %02x%02x%02x", r, g, b);
			int d, y;
			while (1) {
				// dice next color with nearly equal brightness
				rr = rand() % 0xff;
				gg = rand() % 0xff;
				bb = rand() % 0xff;
				y = RR * rr + GG * gg + BB * bb;
				d = abs(rr - r) + abs(gg - g) + abs(bb - b);
				if (YMIN < y && y < YMAX)
					break;
			}
			xlog("LEDSTRIP next target color is %02x%02x%02x y=%d d=%d", rr, gg, bb, y, d);
		}

		if (r != rr) {
			r < rr ? r++ : r--;
			ddp();
			msleep(DELAY_FADE);
			if (SINGLE)
				continue;
		}

		if (g != gg) {
			g < gg ? g++ : g--;
			ddp();
			msleep(DELAY_FADE);
			if (SINGLE)
				continue;
		}

		if (b != bb) {
			b < bb ? b++ : b--;
			ddp();
			msleep(DELAY_FADE);
			if (SINGLE)
				continue;
		}
	}
}

// http://192.168.25.239/cm?cmnd=backlog+power+ON
int ledstrip_on() {
	char url[128];
	snprintf(url, 128, "http://%s/cm?cmnd=backlog+power+ON", STRIP);
	curl_oneshot(url);
	power = 1;
	xlog("LEDSTRIP switched ON");
	return 0;
}

// http://192.168.25.239/cm?cmnd=backlog+power+OFF
int ledstrip_off() {
	char url[128];
	snprintf(url, 128, "http://%s/cm?cmnd=backlog+power+OFF", STRIP);
	curl_oneshot(url);
	power = 0;
	mode = None;
	xlog("LEDSTRIP switched OFF");
	return 0;
}

void ledstrip_toggle() {
	power ? ledstrip_off() : ledstrip_mode_fade();
}

void ledstrip_mode_fade() {
	old_mode = mode;
	old_power = power;
	mode = Fade;
	if (!power)
		ledstrip_on();
}

void ledstrip_mode_blink() {
	old_mode = mode;
	old_power = power;
	mode = Blink;
	if (!power)
		ledstrip_on();
}

void ledstrip_mode_color(uint8_t rr, uint8_t gg, uint8_t bb) {
	old_mode = mode;
	old_power = power;
	r = rr;
	g = gg;
	b = bb;
	mode = Color;
	if (!power)
		ledstrip_on();
	ddp();
}

void ledstrip_blink_red() {
	blink_color = RED;
	ledstrip_mode_blink();
}

void ledstrip_blink_green() {
	blink_color = GREEN;
	ledstrip_mode_blink();
}

void ledstrip_blink_blue() {
	blink_color = BLUE;
	ledstrip_mode_blink();
}

static void loop_mode() {
	switch (mode) {
	case Fade:
		return fade_loop();
	case Blink:
		return blink_loop();
	default:
	}
}

static void loop() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	while (1) {
		msleep(100);
		if (power)
			loop_mode();
	}
}

static void test_fade() {
	r = g = b = 0;
	ddp();
	ledstrip_on();

	for (r = 0; r < 0xff; r++) {
		ddp();
		msleep(10);
	}
	r = g = b = 0;
	ddp();

	for (g = 0; g < 0xff; g++) {
		ddp();
		msleep(10);
	}
	r = g = b = 0;
	ddp();

	for (b = 0; b < 0xff; b++) {
		ddp();
		msleep(10);
	}

	r = g = b = 0;
	ddp();
	ledstrip_off();
}

static int init() {
	// initialize random number generator
	srand(time(NULL));

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sock == 0)
		return xerr("LEDSTRIP Error creating UDP socket");

	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(4048);
	sock_addr_in.sin_addr.s_addr = inet_addr(STRIP);

	return 0;
}

static void stop() {
	if (sock)
		close(sock);
}

int ledstrip_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	init();

	ledstrip_mode_color(rand() % 0xff, rand() % 0xff, rand() % 0xff);
	sleep(3);

	test_fade();

//	ledstrip_blink_red();
//	loop();
//
//	ledstrip_blink_green();
//	loop();
//
//	ledstrip_blink_blue();
//	loop();

	ledstrip_mode_fade();
	loop();

	stop();
	return 0;
}

#ifdef LEDSTRIP_MAIN
int main(int argc, char **argv) {
	return ledstrip_main(argc, argv);
}
#else
MCP_REGISTER(ledstrip, 6, &init, &stop, &loop);
#endif
