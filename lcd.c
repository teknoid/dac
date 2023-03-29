/*****************************************************************************

 i2clcd.c - LCD over I2C library
 Designed for HD44870 based LCDs with I2C expander PCF8574X
 on Atmels AVR MCUs

 Copyright (C) 2006 Nico Eichelmann and Thomas Eichelmann
 2014 clean up by Falk Brunner
 2023 adapted for dac project Copyright (C) Heiko Jehmlich (hje @ jecons.de)

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

 You can contact the authors at info@computerheld.de

 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "utils.h"
#include "i2c.h"
#include "lcd.h"
#include "mcp.h"

#ifndef I2C
#define I2C				"/dev/i2c-11"
#endif

// static const int overflow_mode = LCD_OFLOW_SCROLL;
static const int overflow_mode = LCD_OFLOW_ALTERN;
static int overflow;

static int i2cfd;

static int backlight;
static pthread_t thread;

char *text1, *text2;
int scroll1, scroll2;

//-	Write nibble to display with pulse of enable bit
static void lcd_write(uint8_t value) {
	uint8_t data_out = value << 4 & 0xf0;

	if (value & CMD_RS)
		data_out |= LCD_RS;
	if (value & CMD_RW)
		data_out |= LCD_RW;
	if (backlight)
		data_out |= LCD_LIGHT_N;

	i2c_put(i2cfd, LCD_I2C_DEVICE, data_out | LCD_E);		//-	Set new data and enable to high
	i2c_put(i2cfd, LCD_I2C_DEVICE, data_out);	            //-	Set enable to low
}

//-	Issue a command to the display (use the defined commands above)
static void lcd_command(uint8_t command) {
	lcd_write((command >> 4));
	lcd_write((command & 0x0F));
}

//- turn backlight on
static void lcd_backlight_on() {
	i2c_put(i2cfd, LCD_I2C_DEVICE, LCD_LIGHT_ON);
	backlight = LCD_BACKLIGHT_TIMER;
}

//- turn backlight off
static void lcd_backlight_off() {
	i2c_put(i2cfd, LCD_I2C_DEVICE, LCD_LIGHT_OFF);
	backlight = 0;
}

//-	Put char to atctual cursor position
static void lcd_putchar(char lcddata) {
	lcd_write((lcddata >> 4) | CMD_RS);
	lcd_write((lcddata & 0x0F) | CMD_RS);
}

//-	Go to position (line, column)
static int lcd_gotolc(uint8_t row, uint8_t col) {
	uint8_t lcddata = 0;

	if ((row > LCD_LINES) || (col > LCD_COLS) || ((row == 0) || (col == 0)))
		return 0;

	switch (row) {
	case 1:
		lcddata = LCD_LINE1;
		break;
	case 2:
		lcddata = LCD_LINE2;
		break;
	case 3:
		lcddata = LCD_LINE3;
		break;
	case 4:
		lcddata = LCD_LINE4;
		break;
	}
	lcddata |= 0x80;
	lcddata += (col - 1);
	lcd_command(lcddata);
	return 1;
}

static void lcd_printlxy(const char *text, int row, int x, int y) {
	lcd_gotolc(row, 1);
	for (int i = 0; ((i < LCD_COLS) && i < (y - x)); i++) {
		if (!text[x + i])
			break;
		lcd_putchar(text[x + i]);
	}
}

static void lcd_printl(const char *text, int row) {
	int col = 1;
	lcd_gotolc(row, col);
	while (*text && col++ <= LCD_COLS)
		lcd_putchar(*text++);
}

static void lcd_scroll(int row, const char *text, int *scrollptr) {
	if (text == NULL)
		return;

	int length = strlen(text);
	if (length < LCD_COLS)
		return;

	int to = length - *scrollptr + 3;
	if (to > LCD_COLS)
		*scrollptr += 1;
	else
		*scrollptr = -3;

	if ((*scrollptr + LCD_COLS) <= length)
		lcd_printl(text + *scrollptr, row);
}

static int lcd_find_line_break(const char *text) {
	for (int i = 0; i < strlen(text); i++) {
		if (i <= (LCD_COLS / 2))
			continue;
		if (text[i] == 0x20)
			return i;
		if (i == LCD_COLS)
			return LCD_COLS;
	}
	return LCD_COLS;
}

static void lcd_print_break(const char *text) {
	int x = lcd_find_line_break(text);
	lcd_command(LCD_CLEAR);
	msleep(200);
	lcd_printlxy(text, 1, 0, x);
	lcd_printlxy(text, 2, x + 1, strlen(text));
}

static void lcd_update() {
	if (!overflow)
		return;

	if (overflow_mode == LCD_OFLOW_SCROLL) {
		lcd_scroll(1, text1, &scroll1);
		lcd_scroll(2, text2, &scroll2);
	} else if (overflow_mode == LCD_OFLOW_ALTERN) {
		if (scroll1 == -3)
			lcd_print_break(text1);
		if (scroll1 == 0)
			lcd_print_break(text2);
		if (scroll1++ >= 3)
			scroll1 = -3;
	}
}

void lcd_print(const char *t1, const char *t2) {
	text1 = strdup(t1);
	text2 = strdup(t2);

	lcd_command(LCD_CLEAR);
	msleep(200);
	lcd_backlight_on();

	overflow = strlen(text1) > LCD_COLS || strlen(text2) > LCD_COLS;
	if (!overflow) {
		lcd_printl(text1, 1);
		lcd_printl(text2, 2);
	} else {
		scroll1 = -3;
		scroll2 = -3;
		lcd_update();
	}
}

static void* lcd(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		sleep(1);

		// update the LCD Display - it is very slow so set sleep() to 1 second
		lcd_update();

		if (backlight > 0)
			backlight--;

		if (backlight == 1)
			lcd_backlight_off();
	}
}

static int init() {
	if ((i2cfd = open(I2C, O_RDWR)) < 0)
		return xerr("error opening  %s", I2C);

	lcd_write(CMD_D1 | CMD_D0);	//-	Set interface to 8-bit
	msleep(5);					//-	Wait for more than 4.1ms
	lcd_write(CMD_D1 | CMD_D0);	//-	Set interface to 8-bit
	msleep(0.1);		        //-	Wait for more than 100us
	lcd_write(CMD_D1 | CMD_D0);	//-	Set interface to 8-bit
	lcd_write(CMD_D1);		    //-	Set interface to 4-bit

	//- From now on in 4-bit-Mode
	lcd_command(LCD_LINE_MODE | LCD_5X7);
	lcd_command(LCD_DISPLAYON | LCD_BLINKINGOFF);
	lcd_command(LCD_CLEAR);
	msleep(2);
	lcd_command(LCD_INCREASE | LCD_DISPLAYSHIFTOFF);

	lcd_backlight_on();
	lcd_printl("LCD initialized", 1);
	sleep(1);
	lcd_backlight_off();

	if (pthread_create(&thread, NULL, &lcd, NULL))
		return xerr("Error creating lcd thread");

	xlog("LCD initialized");
	return 0;
}

static void stop() {
	lcd_backlight_off();
	lcd_command(LCD_CLEAR);

	if (pthread_cancel(thread))
		xlog("Error canceling lcd thread");

	if (pthread_join(thread, NULL))
		xlog("Error joining lcd thread");

	if (i2cfd > 0)
		close(i2cfd);
}

MCP_REGISTER(lcd, 2, &init, &stop);

