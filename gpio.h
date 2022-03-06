/*
 * simple mmapped gpio bit-banging
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

// generic GPIO functions
int gpio_init(void);
int gpio_configure(const char *name, int function, int trigger, int initial);
int gpio_get(const char *name);
int gpio_toggle(const char *name);
void gpio_set(const char *name, int value);
void gpio_print(const char *name);
void gpio_close(void);

// generic timer functions
uint32_t gpio_micros();
uint32_t gpio_micros_since(uint32_t *when);
void gpio_delay_micros(uint32_t us);

// application-specific functions
void gpio_flamingo_v1(const char *name, uint32_t message, int bits, int repeat, int pulse);
void gpio_flamingo_v2(const char *name, uint32_t message, int bits, int repeat, int phi, int plo);
void gpio_lirc(const char *name, uint32_t message);
