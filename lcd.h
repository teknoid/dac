/*****************************************************************************

 i2clcd.h - LCD over I2C library
 Designed for HD44870 based LCDs with I2C expander PCF8574X
 on Atmels AVR MCUs

 Copyright (C) 2006 Nico Eichelmann and Thomas Eichelmann
 2014 clean up by Falk Brunner

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

/**
 \mainpage

 \par i2clcd.h - LCD over I2C library
 Designed for HD44870 based LCDs with I2C expander PCF8574X
 on Atmels AVR MCUs

 \author Nico Eichelmann, Thomas Eichelmann, Falk Brunner

 \version 0.11

 \par License:
 \subpage LICENSE "GNU Lesser General Public License"

 \par Files:
 \subpage I2CLCD.H \n
 \subpage I2CLCD.C

 \note Requires I2C-Library from Peter Fleury http://jump.to/fleury

 \par Only testet with the following configuration:
 2x16 Display (Displaytec 162), PCF8574P, ATmega8 @ 8 Mhz \n
 AVR-Studio 4.18, WinAVR20100110 (avr-gcc 4.3.3) \n

 \par PIN-Assignment:
 \verbatim
 Pin assignment is completely free configurable, just set the defines for LCD_D0 ... accordingly
 Example assignment, working with
 PCF8574	<->		LCD
 ----------------------------------------------
 P0		<->		RS
 P1		<->		RW
 P2		<->		E
 P3		<->		LIGHT
 P4		<->		DB4
 P5		<->		DB5
 P6		<->		DB6
 P7		<->		DB7 \endverbatim

 \par Example:
 \code

 #include <stdbool.h>
 #include <stdint.h>
 #include <avr/pgmspace.h>
 #include "main.h"
 #include <util/delay.h>
 #include "i2clcd.h"
 #include "i2cmaster.h"

 char string_flash[] PROGMEM = "Hello Flash!";

 int main(void)
 {
 char string1[] = "Hello World!";

 i2c_init();
 lcd_init();

 lcd_light(true);
 lcd_print(string1);
 lcd_nextline();
 lcd_print_P(PSTR("I2CLCD V0.11"));

 // always set all three parameters  (OM/OFF) when using this command
 lcd_command(LCD_DISPLAYON | LCD_CURSORON | LCD_BLINKINGON);
 _delay_ms(1000);

 lcd_command(LCD_CLEAR);
 _delay_ms(2);
 lcd_print_P(string_flash);
 lcd_printlc_P(2, 2, string_flash);

 //-	Endless loop

 while (1) {

 }
 }
 \endcode
 \page LICENSE GNU Lesser General Public License
 \include ./lgpl.txt
 \page I2CLCD.H i2clcd.h
 \include ./i2clcd.h
 \page I2CLCD.C i2clcd.c
 \include ./i2clcd.c
 */

#ifndef _I2CLCD_H
#define _I2CLCD_H

//-------------------------------------------------------------------------------------------------------------------

//--Display-Configuration-Settings-----------------------------------------------------------------------------------

/** \defgroup DISPLAY_CONFIGURATION DISPLAY CONFIGURATION
 Change this settings to your configuration. \n
 Supported LCD sizes range from 1x8 to 4x20. \n
 4x40 is not supported.\n
 */
/*@{*/
#define LCD_I2C_DEVICE		0x27	    /**< Change this to the address of your expander */
#define LCD_LINES			2	        /**< Enter the number of lines of your display here */
#define LCD_COLS			16	        /**< Enter the number of columns of your display here */
#define LCD_LINE_MODE       LCD_2LINE   /**< Enter line mode your display here */

#define LCD_LINE1			0x00	    /**< This should be 0x00 on all displays */
#define LCD_LINE2			0x40	    /**< Change this to the address for line 2 on your display */
#define LCD_LINE3			0x40	    /**< Change this to the address for line 3 on your display */
#define LCD_LINE4			0x60	    /**< Change this to the address for line 4 on your display */
#define LCD_BACKLIGHT_TIMER	120	    	/**< 120 * 250ms = 30 seconds */
/*@}*/

#if LCD_LINES > 4
    #error "#define LCD_LINES must be less or equal to 4"
#endif

#if LCD_COLS > 20
    #error "#define LCD_COLS must be less or equal to 20"
#endif

//-------------------------------------------------------------------------------------------------------------------

//--The-following-definitions-are-corresponding-to-the-PIN-Assignment-(see-above)------------------------------------

/** \defgroup PIN_ASSIGNMENT PIN ASSIGNMENT
 This pin assignment shows how the display is connected to the PCF8574.
 Set the definition to match your hardware setup. Any assignment is possible, but avoid mapping of two signal to one pin!
 */
/*@{*/
#define LCD_D4_PIN			4	/**< LCD-Pin D4 is connected to P4 on the PCF8574 */
#define LCD_D5_PIN			5	/**< LCD-Pin D5 is connected to P5 on the PCF8574 */
#define LCD_D6_PIN			6	/**< LCD-Pin D6 is connected to P6 on the PCF8574 */
#define LCD_D7_PIN			7	/**< LCD-Pin D7 is connected to P7 on the PCF8574 */
#define LCD_RS_PIN			0	/**< LCD-Pin RS is connected to P0 on the PCF8574 */
#define LCD_RW_PIN			1	/**< LCD-Pin RW is connected to P1 on the PCF8574 */
#define LCD_E_PIN			2	/**< LCD-Pin E is connected to P2 on the PCF8574 */
#define LCD_LIGHT_PIN		3	/**< LCD backlight is connected to P3 on the PCF8574, low active */
/*@}*/

//-------------------------------------------------------------------------------------------------------------------
/** \defgroup DEFINED_BITS DEFINED BITS
 With each read/write operation to/from the display one bytes is send/received. \n
 It contains the control bits RS, RW, LIGHT_N and ENABLE and four data bits.
 */
/*@{*/

#define LCD_D4				(1 << LCD_D4_PIN)	/**< bit 4 in 2nd lower nibble */
#define LCD_D5				(1 << LCD_D5_PIN)	/**< bit 5 in 2nd lower nibble */
#define LCD_D6				(1 << LCD_D6_PIN)	/**< bit 6 in 2nd lower nibble */
#define LCD_D7				(1 << LCD_D7_PIN)	/**< bit 7 in 2nd lower nibble */

#define LCD_RS				(1 << LCD_RS_PIN)	/**< RS-bit in 1st and 2nd higher nibble */
#define LCD_RW				(1 << LCD_RW_PIN)	/**< RW-bit in 1st and 2nd higher nibble */
#define LCD_LIGHT_N			(1 << LCD_LIGHT_PIN)/**< LCD backlight control, low active */
#define LCD_E				(1 << LCD_E_PIN)	/**< E-bit in 1st and 2nd higher nibble */

/*@}*/

// data & control bits for internal use, do not change!
#define CMD_D0				(1 << 0)	/**< bit 0 in lower nibble */
#define CMD_D1				(1 << 1)	/**< bit 1 in lower nibble */
#define CMD_D2				(1 << 2)	/**< bit 2 in lower nibble */
#define CMD_D3				(1 << 3)	/**< bit 3 in lower nibble */
#define CMD_RS				(1 << 4)	/**< RS-bit */
#define CMD_RW				(1 << 5)	/**< RW-bit */

/** \defgroup DEFINED_READ_MODES DEFINED READ MODES
 */
/*@{*/
#define LCD_ADDRESS			0	/**< Used for reading the address-counter and busy-flag */
#define LCD_DATA			1	/**< Used for reading data */
/*@}*/

//-LCD-COMMANDS------------------------------------------------------------------------------------------------------
/** \defgroup DEFINED_COMMANDS DEFINED COMMANDS
 These defined commands should be used to configure the display. \n
 Don't use commands from different categories together. \n

 Configuration commands from one category should get combined to one command.
 \par Example:
 \code lcd_command(LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKINGON); \endcode

 The category modes like LCD_SHIFTMODE and LCD_CONFIGURATION can be omitted.
 */
/*@{*/

/** @name GENERAL COMMANDS */
/*@{*/
#define LCD_CLEAR			0x01	/**< Clear screen */
#define LCD_HOME			0x02	/**< Cursor move to first digit */
/*@}*/

/** @name ENTRYMODES */
/*@{*/
#define LCD_ENTRYMODE		0x04					/**< Set entrymode */
#define LCD_INCREASE		LCD_ENTRYMODE | 0x02	/**<	Set cursor move direction -- Increase */
#define LCD_DECREASE		LCD_ENTRYMODE | 0x00	/**<	Set cursor move direction -- Decrease */
#define LCD_DISPLAYSHIFTON	LCD_ENTRYMODE | 0x01	/**<	Display is shifted */
#define LCD_DISPLAYSHIFTOFF	LCD_ENTRYMODE | 0x00	/**<	Display is not shifted */
/*@}*/

/** @name DISPLAYMODES */
/*@{*/
#define LCD_DISPLAYMODE		0x08					/**< Set displaymode */
#define LCD_DISPLAYON		LCD_DISPLAYMODE | 0x04	/**<	Display on */
#define LCD_DISPLAYOFF		LCD_DISPLAYMODE | 0x00	/**<	Display off */
#define LCD_CURSORON		LCD_DISPLAYMODE | 0x02	/**<	Cursor on */
#define LCD_CURSOROFF		LCD_DISPLAYMODE | 0x00	/**<	Cursor off */
#define LCD_BLINKINGON		LCD_DISPLAYMODE | 0x01	/**<	Blinking on */
#define LCD_BLINKINGOFF		LCD_DISPLAYMODE | 0x00	/**<	Blinking off */
/*@}*/

/** @name SHIFTMODES */
/*@{*/
#define LCD_SHIFTMODE		0x10					/**< Set shiftmode */
#define LCD_DISPLAYSHIFT	LCD_SHIFTMODE | 0x08	/**<	Display shift */
#define LCD_CURSORMOVE		LCD_SHIFTMODE | 0x00	/**<	Cursor move */
#define LCD_RIGHT			LCD_SHIFTMODE | 0x04	/**<	Right shift */
#define LCD_LEFT			LCD_SHIFTMODE | 0x00	/**<	Left shift */
/*@}*/

/** @name DISPLAY_CONFIGURATION */
/*@{*/
#define LCD_CONFIGURATION	0x20					/**< Set function */
#define LCD_8BIT		LCD_CONFIGURATION | 0x10	/**<	8 bits interface */
#define LCD_4BIT		LCD_CONFIGURATION | 0x00	/**<	4 bits interface */
#define LCD_4LINE						    0x09	/**<	4 line display */
#define LCD_2LINE		LCD_CONFIGURATION | 0x08	/**<	2 line display */
#define LCD_1LINE		LCD_CONFIGURATION | 0x00	/**<	1 line display */
#define LCD_5X10		LCD_CONFIGURATION | 0x04	/**<	5 X 10 dots */
#define LCD_5X7			LCD_CONFIGURATION | 0x00	/**<	5 X 7 dots */

#define LCD_LIGHT_ON	LCD_LIGHT_N
#define LCD_LIGHT_OFF	0x00
/*@}*/

//-------------------------------------------------------------------------------------------------------------------
/*@}*/

//-FUNCTIONS---------------------------------------------------------------------------------------------------------
/** \defgroup FUNCTIONS_INTERNAL INTERNAL FUNCTIONS */
/*@{*/

/**
 \brief Write data to i2c (for internal use)
 \param value byte to send over i2c
 \return none
 */
void lcd_write_i2c(uint8_t value);

/**
 \brief Write nibble to display with toggle of enable-bit
 \param value the upper nibble represents  RS, RW pins and the lower nibble contains data
 \return none
 */
void lcd_write(uint8_t value);

/**
 \brief Read data from i2c (for internal use)
 \retval "uint8_t" byte received over i2c
 */
uint8_t lcd_read_i2c(void);

/**
 \brief Read data from display over i2c (for internal use)
 \param mode ADDRESS for cursor address and busy flag or DATA for display data
 \retval "uint8_t" lower nibble contains data D0 to D3 pins or D4 to D7 pins
 */
uint8_t lcd_read(int mode);

/**
 \brief Read one byte over i2c from display
 \param mode ADDRESS for cursor address and busy flag or DATA for display data
 \retval "uint8_t" the byte received from the display
 */
uint8_t lcd_getbyte(int mode);

//-------------------------------------------------------------------------------------------------------------------
/*@}*/

//-FUNCTIONS---------------------------------------------------------------------------------------------------------
/** \defgroup FUNCTIONS FUNCTIONS */
/*@{*/

/**
 \brief Display initialization sequence
 \return none
 */
int lcd_init();
void lcd_close();

/**
 \brief Issue a command to the display
 \param command use the defined commands above
 \return none
 */
void lcd_command(uint8_t command);

/**
 \brief Go to position
 \param line 1st line is 1 and last line = LCD_LINES
 \param col 1st col is 1 and last col = LCD_colS
 \retval true if successfull
 \retval false if not successfull
 */
int lcd_gotolc(uint8_t line, uint8_t col);

/**
 \brief Put char to cursor position
 \param value the char to print
 \return none
 */
void lcd_putchar(char value);

/**
 \brief Put char to position
 \param line the line to put the char to
 \param col the column to put the char to
 \param value the char to print
 \retval true if successfull
 \retval false if not successfull
 */
int lcd_putcharlr(uint8_t line, uint8_t col, char value);

/**
 \brief Print string to cursor position
 \param *string pointer to the string to print
 \return none
 */
void lcd_print(char *string);

/**
 \brief Print string to position (If string is longer than LCD_COLS overwrite first chars in line)
 \param line the line to put the string to
 \param col the column to put the string to
 \param *string pointer to the string to print
 \retval true if successfull
 \retval false if not successfull
 */
int lcd_printlc(uint8_t line, uint8_t col, char *string);

/**
 \brief Print string from Flash to position (If string is longer than LCD_COLS overwrite first chars in line)
 \param line the line to put the string to
 \param col the column to put the string to
 \param *string pointer to the string to print
 \retval true if successfull
 \retval false if not successfull
 */
int lcd_printlc_P(uint8_t line, uint8_t col, char *string);

/**
 \brief Print string to position (If string is longer than LCD_COLS continue in next line)
 \param line the line to put the string to
 \param col the col to put the string to
 \param *string pointer to the string to print
 \retval true if successfull
 \retval false if not successfull
 */
int lcd_printlcc(uint8_t line, uint8_t col, char *string);

/**
 \brief Print string from flash to position (If string is longer than LCD_COLS continue in next line)
 \param line the line to put the string to
 \param col the col to put the string to
 \param *string pointer to the string to print
 \retval true if successfull
 \retval false if not successfull
 */
int lcd_printlcc_P(uint8_t line, uint8_t col, char *string);

/**
 \brief Go to nextline (if next line > LCD_LINES return false)
 \retval true if successfull
 \retval false if not successfull
 */
int lcd_nextline(void);

/**
 \brief Get line and col of the cursor position
 \param *line pointer to the target byte for line
 \param *col pointer to the target byte for column
 \retval true if successfull
 \retval false if not successfull
 */
int lcd_getlc(uint8_t *line, uint8_t *col);

/**
 \brief Check if LCD is busy
 \retval true if busy
 \retval false if not busy
 */
int lcd_busy(void);

/**
 \brief Turn backlight ON/OFF
 \param light true to tun light ON
 \param light false to turn light OFF
 \return none
 */
void lcd_backlight_on();
void lcd_backlight_off();

//-------------------------------------------------------------------------------------------------------------------
/*@}*/

#endif
