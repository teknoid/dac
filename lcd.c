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
#include <pthread.h>

#include "utils.h"
#include "i2c.h"
#include "lcd.h"
#include "mcp.h"

static int i2cfd;

static int backlight;
static pthread_t thread;

//-	Write nibble to display with pulse of enable bit
void lcd_write(uint8_t value) {
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

//-	Read data from display over i2c (lower nibble contains LCD data)
uint8_t lcd_read(int mode) {
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

//-	Read one complete byte via i2c from display
uint8_t lcd_getbyte(int mode) {
	uint8_t hi = lcd_read(mode);
	uint8_t lo = lcd_read(mode);
	return (hi << 4) + (lo & 0x0F);
}

//-	Issue a command to the display (use the defined commands above)
void lcd_command(uint8_t command) {
	lcd_write((command >> 4));
	lcd_write((command & 0x0F));
}

//-	Print string to cursor position
void lcd_print(char *string) {
	while (*string)
		lcd_putchar(*string++);
}

//-	Put char to atctual cursor position
void lcd_putchar(char lcddata) {
	lcd_write((lcddata >> 4) | CMD_RS);
	lcd_write((lcddata & 0x0F) | CMD_RS);
}

//-	Put char to position
int lcd_putcharlc(uint8_t line, uint8_t col, char value) {
	if (!lcd_gotolc(line, col))
		return 0;
	lcd_putchar(value);
	return 1;
}

//-	Print string to position (If string is longer than LCD_COLS overwrite first chars)(line, row, string)
int lcd_printlc(uint8_t line, uint8_t col, char *string) {
	if (!lcd_gotolc(line, col))
		return 0;

	while (*string) {
		lcd_putchar(*string++);
		col++;
		if (col > LCD_COLS) {
			col = 1;
			lcd_gotolc(line, col);
		}
	}
	return 1;
}

//-	Print string to position (If string is longer than LCD_COLS continue in next line)
int lcd_printlcc(uint8_t line, uint8_t col, char *string) {
	if (!lcd_gotolc(line, col))
		return 0;

	while (*string) {
		lcd_putchar(*string++);
		col++;
		if (col > LCD_COLS) {
			line++;
			col = 1;
			if (line > LCD_LINES) {
				line = 1;
			}
			lcd_gotolc(line, col);
		}
	}
	return 1;
}

//-	Go to position (line, column)
int lcd_gotolc(uint8_t line, uint8_t col) {
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

//-	Go to nextline (if next line > (LCD_LINES-1) return 0)
int lcd_nextline(void) {
	uint8_t line, col;

	lcd_getlc(&line, &col);
	if (!lcd_gotolc(line + 1, 1))
		return 0;
	else
		return 1;
}

//-	Get line and row (target byte for line, target byte for row)
int lcd_getlc(uint8_t *line, uint8_t *col) {
	uint8_t lcddata = lcd_getbyte(LCD_ADDRESS);
	if (lcddata & (1 << 7))
		return 0;       // LCD busy

	if (lcddata >= LCD_LINE1 && lcddata < (LCD_LINE1 + LCD_COLS)) {
		*line = 1;
		*col = lcddata - LCD_LINE1 + 1;
		return 1;
	} else if (lcddata >= LCD_LINE2 && lcddata < (LCD_LINE2 + LCD_COLS)) {
		*line = 2;
		*col = lcddata - LCD_LINE2 + 1;
		return 1;
	} else if (lcddata >= LCD_LINE3 && lcddata < (LCD_LINE3 + LCD_COLS)) {
		*line = 3;
		*col = lcddata - LCD_LINE3 + 1;
		return 1;
	} else if (lcddata >= LCD_LINE4 && lcddata < (LCD_LINE4 + LCD_COLS)) {
		*line = 4;
		*col = lcddata - LCD_LINE4 + 1;
		return 1;
	}

	return 0;
}

//- turn backlight on
void lcd_backlight_on() {
	i2c_put(i2cfd, LCD_I2C_DEVICE, LCD_LIGHT_ON);
	backlight = LCD_BACKLIGHT_TIMER;
}

//- turn backlight off
void lcd_backlight_off() {
	i2c_put(i2cfd, LCD_I2C_DEVICE, LCD_LIGHT_OFF);
	backlight = 0;
}

//-	Check if busy
int lcd_busy() {
	uint8_t state = lcd_getbyte(LCD_ADDRESS);
	return (state & (1 << 7));
}

static void* lcd(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		msleep(250);

		if (backlight > 0)
			backlight--;

		if (backlight == 1)
			lcd_backlight_off();
	}
}

static int init() {
	if ((i2cfd = open(I2C, O_RDWR)) < 0)
		return xerr("I2C BUS error");

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
	lcd_printlc(1, 1, "LCD initialized");
	msleep(1000);
	lcd_backlight_off();

	if (pthread_create(&thread, NULL, &lcd, NULL))
		return xerr("Error creating lcd thread");

	xlog("LCD initialized");
	return 0;
}

static void destroy() {
	lcd_backlight_off();
	lcd_command(LCD_CLEAR);

	if (pthread_cancel(thread))
		xlog("Error canceling lcd thread");

	if (pthread_join(thread, NULL))
		xlog("Error joining lcd thread");

	if (i2cfd > 0)
		close(i2cfd);
}

MCP_REGISTER(lcd, 1, &init, &destroy);

