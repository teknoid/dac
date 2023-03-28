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

//static const int overflow_mode = LCD_OFLOW_SCROLL;
static const int overflow_mode = LCD_OFLOW_ALTERN;
static int overflow;

static int i2cfd;

static int backlight;
static pthread_t thread;

char *line1, *line2;
int scroll1, scroll2;

//-	Read data from display over i2c (lower nibble contains LCD data)
static uint8_t lcd_read(int mode) {
	uint8_t lcddata, data;

	if (mode == LCD_DATA)
		lcddata = (LCD_E | LCD_RS | LCD_RW | LCD_D4 | LCD_D5 | LCD_D6 | LCD_D7);
	else
		lcddata = (LCD_E | LCD_RW | LCD_D4 | LCD_D5 | LCD_D6 | LCD_D7);

	if (backlight)
		lcddata |= LCD_LIGHT_N;

	i2c_put(i2cfd, LCD_I2C_DEVICE, lcddata);
	lcddata = i2c_get(i2cfd, LCD_I2C_DEVICE);

	data = 0;
	// map data from LCD pinout to internal positions
	if (lcddata & LCD_D4)
		data |= CMD_D0;
	if (lcddata & LCD_D5)
		data |= CMD_D1;
	if (lcddata & LCD_D6)
		data |= CMD_D2;
	if (lcddata & LCD_D7)
		data |= CMD_D3;

	lcddata = 0;
	if (backlight)
		lcddata |= LCD_LIGHT_N;
	i2c_put(i2cfd, LCD_I2C_DEVICE, lcddata);

	return data;
}

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

//-	Read one complete byte via i2c from display
static uint8_t lcd_getbyte(int mode) {
	uint8_t hi = lcd_read(mode);
	uint8_t lo = lcd_read(mode);
	return (hi << 4) + (lo & 0x0F);
}

//-	Check if busy
static int lcd_busy() {
	uint8_t state = lcd_getbyte(LCD_ADDRESS);
	return (state & (1 << 7));
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

// clear all lines
static void lcd_clear() {
	lcd_command(LCD_CLEAR);
	while (lcd_busy())
		msleep(1);
}

//-	Put char to atctual cursor position
static void lcd_putchar(char lcddata) {
	lcd_write((lcddata >> 4) | CMD_RS);
	lcd_write((lcddata & 0x0F) | CMD_RS);
}

//-	Go to position (line, column)
static int lcd_gotolc(uint8_t line, uint8_t col) {
	uint8_t lcddata = 0;

	if ((line > LCD_LINES) || (col > LCD_COLS) || ((line == 0) || (col == 0)))
		return 0;

	switch (line) {
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

static void lcd_printl(int line, const char *text) {
	int col = 1;
	lcd_gotolc(line, col);
	while (*text && col++ <= LCD_COLS)
		lcd_putchar(*text++);
}

static void lcd_scroll(int line, const char *text, int *scrollptr) {
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
		lcd_printl(line, text + *scrollptr);
}

static void lcd_alternate() {
	if (scroll1 == -3) {
		lcd_clear();
		lcd_printl(1, line1);
		lcd_printl(2, line1 + LCD_COLS);
	}

	if (scroll1 == 0) {
		lcd_clear();
		lcd_printl(1, line2);
		lcd_printl(2, line2 + LCD_COLS);
	}

	if (scroll1++ >= 3)
		scroll1 = -3;
}

static void lcd_update() {
	if (!overflow)
		return;

	if (overflow_mode == LCD_OFLOW_SCROLL) {
		lcd_scroll(1, line1, &scroll1);
		lcd_scroll(2, line2, &scroll2);
	} else if (overflow_mode == LCD_OFLOW_ALTERN) {
		lcd_alternate();
	}
}

void lcd_print(const char *l1, const char *l2) {
	if (line1 != NULL)
		free(line1);
	line1 = strdup(l1);

	if (line2 != NULL)
		free(line2);
	line2 = strdup(l2);

	lcd_clear();
	lcd_backlight_on();

	overflow = strlen(l1) > LCD_COLS || strlen(l2) > LCD_COLS;
	if (!overflow) {
		lcd_printl(1, line1);
		lcd_printl(2, line2);
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
	lcd_printl(1, "LCD initialized");
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

