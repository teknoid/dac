/***
 *
 * Library for encoding and decding of 433MHz ELRO Flamingo home device messages
 *
 * (C) Copyright 2022 Heiko Jehmlich <hje@jecons.de>
 *
 * tested with following devices
 *
 * FA500R REMOTE
 * FA500S WIRELESS SWITCH UNIT
 * SF-500R CONTROL
 * SF-500P SWITCH
 *
 *
 * FA500R 28bit message pattern:
 *
 * 0000 0000000000000000 0000 XXXX	channel
 * 0000 0000000000000000 00XX 0000	command
 * 0000 0000000000000000 XX00 0000	rolling code id
 * 0000 XXXXXXXXXXXXXXXX 0000 0000	transmitter id
 * XXXX 0000000000000000 0000 0000	payload
 *
 *
 * SF-500R 32bit message pattern - guessed - looks like transmitter id is moved left
 *
 * 0000000000000000 00000000 0000 XXXX	channel
 * 0000000000000000 00000000 XXXX 0000	command
 * 0000000000000000 XXXXXXXX 0000 0000	payload
 * XXXXXXXXXXXXXXXX 00000000 0000 0000	transmitter id
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>

#include "mcp.h"
#include "gpio.h"
#include "utils.h"
#include "frozen.h"
#include "flamingo.h"

#define BUFFER				128

// flamingo encryption key
static const unsigned char CKEY[16] = { 9, 6, 3, 8, 10, 0, 2, 12, 4, 14, 7, 5, 1, 15, 11, 13 };

// flamingo decryption key (invers encryption key - exchanged index & value)
static const unsigned char DKEY[16] = { 5, 12, 6, 2, 8, 11, 1, 10, 3, 0, 4, 14, 7, 15, 9, 13 };

static const char *fmt_message28 = "FLAMINGO F28 0x%08lx id=%04x, chan=%02d, cmd=%d, pay=0x%02x, roll=%d";
static const char *fmt_message32 = "FLAMINGO F32 0x%08lx id=%04x, chan=%02d, cmd=%d, pay=0x%02x";

static uint32_t encrypt(uint32_t message) {
	uint32_t code = 0;
	uint8_t n[7];
	int i, r, idx;

	// split into nibbles
	for (i = 0; i < sizeof(n); i++)
		n[i] = message >> (4 * i) & 0x0F;

	// XOR encryption 2 rounds
	for (r = 0; r <= 1; r++) {					// 2 encryption rounds
		idx = (n[0] - r + 1) & 0x0F;
		n[0] = CKEY[idx];						// encrypt first nibble
		for (i = 1; i <= 5; i++) {				// encrypt 4 nibbles
			idx = ((n[i] ^ n[i - 1]) - r + 1) & 0x0F;
			n[i] = CKEY[idx];					// crypted with predecessor & key
		}
	}
	n[6] = n[6] ^ 9;							// no encryption

	// build encrypted message
	code = (n[6] << 24) | (n[5] << 20) | (n[4] << 16) | (n[3] << 12) | (n[2] << 8) | (n[1] << 4) | n[0];

	// shift 2 bits right & copy lowest 2 bits of n[0] in msg bit 27/28
	code = (code >> 2) | ((code & 3) << 0x1A);

	return code;
}

static uint32_t decrypt(uint32_t code) {
	uint32_t message = 0;
	uint8_t n[7];
	int i, r;

	//shift 2 bits left & copy bit 27/28 to bit 1/2
	code = ((code << 2) & 0x0FFFFFFF) | ((code & 0xC000000) >> 0x1A);

	// split into nibbles
	for (i = 0; i < sizeof(n); i++)
		n[i] = code >> (4 * i) & 0x0F;

	n[6] = n[6] ^ 9;							// no decryption
	// XOR decryption 2 rounds
	for (r = 0; r <= 1; r++) {					// 2 decryption rounds
		for (i = 5; i >= 1; i--) {					// decrypt 4 nibbles
			n[i] = ((DKEY[n[i]] - r) & 0x0F) ^ n[i - 1];	// decrypted with predecessor & key
		}
		n[0] = (DKEY[n[0]] - r) & 0x0F;			// decrypt first nibble
	}

	// build message
	for (i = sizeof(n) - 1; i >= 0; i--) {
		message |= n[i];
		if (i)
			message <<= 4;
	}

	return message;
}

void flamingo28_decode(uint32_t code, uint16_t *xmitter, uint8_t *command, uint8_t *channel, uint8_t *payload, uint8_t *rolling) {
	uint32_t message = decrypt(code);
	*payload = (message >> 24) & 0x0F;
	*xmitter = (message >> 8) & 0xFFFF;
	*rolling = (message >> 6) & 0x03;
	*command = (message >> 4) & 0x03;
	*channel = message & 0x0F;

	xlog("FLAMINGO F28 %04x %02d %d 0 %s <= 0x%08x <= 0x%08x", *xmitter, *channel, *command, printbits32(message, SPACEMASK_FA500), message, code);
	xlog(fmt_message28, message, *xmitter, *channel, *command, *payload, *rolling);
}

void flamingo32_decode(uint32_t message, uint16_t *xmitter, uint8_t *command, uint8_t *channel, uint8_t *payload) {
	*payload = (message >> 24) & 0x0F;
	*xmitter = (message >> 8) & 0xFFFF;
	*command = (message >> 4) & 0x0F;
	*channel = message & 0x0F;

	xlog("FLAMINGO F32 %04x %02d %d %s <= 0x%08x", *xmitter, *channel, *command, printbits32(message, SPACEMASK_FA500), message);
	xlog(fmt_message32, message, *xmitter, *channel, *command, *payload);
}

uint32_t flamingo28_encode(uint16_t xmitter, uint8_t channel, uint8_t command, uint8_t payload, uint8_t rolling) {
	uint32_t message = (payload & 0x0F) << 24 | xmitter << 8 | (rolling << 6 & 0xC0) | (command & 0x03) << 4 | (channel & 0x0F);
	uint32_t code = encrypt(message);

	xlog("FLAMINGO F28 %04x %02d %d %d %s => 0x%08x => 0x%08x", xmitter, channel, command, rolling, printbits32(message, SPACEMASK_FA500), message, code);
	xlog(fmt_message28, 0, code, xmitter, channel, command, payload, rolling);

	return code;
}

uint32_t flamingo32_encode(uint16_t xmitter, uint8_t channel, uint8_t command, uint8_t payload) {
	uint32_t message = (payload & 0x0F) << 24 | xmitter << 8 | (command & 0x0F) << 4 | (channel & 0x0F);

	xlog("FLAMINGO F32 %04x %02d %d %s => 0x%08x", xmitter, channel, command, printbits32(message, SPACEMASK_FA500), message);
	xlog(fmt_message32, 0, message, xmitter, channel, command, payload);

	return message;
}

// TODO
uint32_t flamingo24_encode(uint16_t xmitter, uint8_t channel, uint8_t command, uint8_t payload) {
	return 0;
}

int flamingo_test(int argc, char **argv) {
	uint32_t bruteforce[4] = { 0x0e6bd68d, 0x0e7be29d, 0x0e7be29d, 0x0e763e15 };
	uint32_t deadbeef[3] = { 0x0000dead, 0x000beef0, 0x0affe000 };
	uint32_t code, message;
	uint16_t xmitter;
	uint8_t command, channel, payload, rolling;

	xlog("*** test printbits ***");
	xlog("printbits8 0x55=%s 0xAA=%s ", printbits(0x55), printbits(0xAA));
	xlog("printbits32 0xAFFE=%s", printbits32(0xAFFE, SPACEMASK32));
	xlog("printbits64 0xDEADBEEF=%s", printbits64(0xDEADBEEF, SPACEMASK64));

	xlog("*** test message encode + decode + re-encode ***");
	code = flamingo28_encode(REMOTES[0], 2, 1, 0x05, 0);
	flamingo28_decode(code, &xmitter, &command, &channel, &payload, &rolling);
	code = flamingo32_encode(REMOTES[0], 2, 1, 0x05);
	flamingo32_decode(code, &xmitter, &command, &channel, &payload);

	xlog("*** test rolling code encryption & decryption ***");
	for (int r = 0; r < 4; r++) {
		code = flamingo28_encode(REMOTES[0], 2, 0, 0, r);
		flamingo28_decode(code, &xmitter, &command, &channel, &payload, &rolling);
	}

	xlog("*** test brute-force encryption ***");
	for (int y = 0; y < 0x0F; y++)
		for (int x = 0; x < 0xFF; x++) {
			message = y << 24 | REMOTES[0] << 8 | x;
			code = encrypt(message);
			if (code == bruteforce[0] || code == bruteforce[1] || code == bruteforce[2] || code == bruteforce[4])
				xlog("%s => 0x%08x => 0x%08x", printbits32(message, SPACEMASK_FA500), message, code);
		}

	xlog("*** test dead+beef+affe ***");
	for (int i = 0; i < ARRAY_SIZE(deadbeef); i++) {
		code = deadbeef[i];
		flamingo28_decode(code, &xmitter, &command, &channel, &payload, &rolling);
	}

	if (argc > 2)
		for (int i = 2; i < argc; i++)
			if (strlen(argv[i]) > 5) {
				xlog("*** decode command line argument %s ***", argv[i]);
				flamingo28_decode(strtoul(argv[i], NULL, 0), &xmitter, &command, &channel, &payload, &rolling);
			}

	return EXIT_SUCCESS;
}

void flamingo_send_FA500(int remote, uint8_t channel, uint8_t command, uint8_t rolling) {
	if (remote < 1 || remote > ARRAY_SIZE(REMOTES))
		return;

	if (channel < 'A' || channel > 'P')
		return;

	uint16_t transmitter = REMOTES[remote - 1];
	if (0 <= rolling && rolling <= 4) {
		// send specified rolling code
		uint32_t c28 = flamingo28_encode(transmitter, channel - 'A' + 1, command ? 2 : 0, 0, rolling);
		gpio_flamingo_v1(TX, c28, 28, 4, T1);

		uint32_t m32 = flamingo32_encode(transmitter, channel - 'A' + 1, command, 0);
		gpio_flamingo_v2(TX, m32, 32, 3, T2H, T2L);
	} else {
		// send all rolling codes in sequence
		for (int r = 0; r < 4; r++) {
			uint32_t c28 = flamingo28_encode(transmitter, channel - 'A' + 1, command ? 2 : 0, 0, r);
			gpio_flamingo_v1(TX, c28, 28, 4, T1);

			uint32_t m32 = flamingo32_encode(transmitter, channel - 'A' + 1, command, 0);
			gpio_flamingo_v2(TX, m32, 32, 3, T2H, T2L);
			sleep(1);
		}
	}
}

// TODO
void flamingo_send_SF500(int remote, uint8_t channel, uint8_t command) {
	if (remote < 1 || remote > ARRAY_SIZE(REMOTES))
		return;

	uint16_t transmitter = REMOTES[remote - 1];
	uint32_t message = flamingo24_encode(transmitter, channel - 'A' + 1, command, 0);
	gpio_flamingo_v2(TX, message, 32, 5, T2H, T2L);
}

static int usage() {
	xlog("Usage: flamingo <remote> <channel> <command> [rolling]\n");
	xlog("    <remote>  1, 2, 3, ...\n");
	xlog("    <channel> A, B, C, D\n");
	xlog("    <command> 0 - off, 1 - on\n");
	xlog("    [rolling] rolling code index, 0...3\n");
	return EXIT_FAILURE;
}

static int init() {
	// GPIO pin connected to 433MHz sender module
	gpio_configure(TX, 1, 0, 0);
	return 0;
}

static void stop() {
}

int flamingo_main(int argc, char *argv[]) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	if (argc < 1)
		return usage();

	// parse command line arguments
	int c;
	while ((c = getopt(argc, argv, "t")) != -1) {
		switch (c) {
		case 't':
			flamingo_test(argc, argv);
			return EXIT_SUCCESS;
		}
	}

	if (argc >= 4) {

		// SEND mode

		// remote 1, 2, 3, ...
		int remote = atoi(argv[1]);
		if (remote < 1 || remote > sizeof(REMOTES)) {
			xlog("unknown remote %i\n", remote);
			usage();
			return EINVAL;
		}

		// channel A, B, C, D
		char *c = argv[2];
		char channel = toupper(c[0]);
		if (channel < 'A' || channel > 'D') {
			xlog("channel not supported %c\n", channel);
			usage();
			return EINVAL;
		}

		// command 0 = off, 1 = on
		int command = atoi(argv[3]);
		if (!(command == 0 || command == 1)) {
			xlog("wrong command %i\n", command);
			usage();
			return EINVAL;
		}

		// optional: send rolling code index
		int rolling = -1;
		if (argv[4] != NULL) {
			rolling = atoi(argv[4]);
			if (rolling < 0 || rolling > 3) {
				xlog("wrong rolling code index %i\n", rolling);
				usage();
				return EINVAL;
			}
		}

		// initialize without receive support (clear pattern + default handler)
		// elevate realtime priority for sending thread
		if (elevate_realtime(3) < 0)
			return -2;

		init();
		flamingo_send_FA500(remote, channel, command, rolling);
		stop();

		return EXIT_SUCCESS;
	}

	return usage();
}

#ifdef FLAMINGO_MAIN
// gpio-bcm2835.c needs mcp_register()
typedef int (*init_t)();
void mcp_register(const char *name, const int prio, const void *init, const void *stop, const void *loop) {
	xlog("call init() for %s", name);
	init_t xinit = init;
	(xinit)();
}
int main(int argc, char **argv) {
	return flamingo_main(argc, argv);
}
#else
MCP_REGISTER(flamingo, 2, &init, &stop, NULL);
#endif
