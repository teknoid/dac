#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>

#include "utils.h"
#include "gpio.h"

#define GPIO_DAC_POWER		"PG11"
#define GPIO_DAC_MCLK		"PA6"

int main(int argc, char **argv) {
	xlog_init(XLOG_SYSLOG, NULL);
	xlog("MCP initializing");

	if (gpio_init() < 0)
		return -1;

	int power = gpio_configure(GPIO_DAC_POWER, 1, 0, -1);
	xlog("DAC power is %s", power ? "ON" : "OFF");

	gpio_configure(GPIO_DAC_MCLK, 3, 0, -1);
	gpio_print(GPIO_DAC_MCLK);

//	gpio_x();

	if (power)
		gpio_set(GPIO_DAC_POWER, 0);
	else
		gpio_set(GPIO_DAC_POWER, 1);

}
