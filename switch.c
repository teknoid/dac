#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "utils.h"
#include "gpio.h"

#define GPIO_DAC_POWER		"PG11"
#define GPIO_DAC_MCLK		"PA6"

typedef int (*init_t)();

// gpio-bcm2835.c needs mcp_register()
void mcp_register(const char *name, const int prio, const void *init, const void *stop) {
	xlog("call init() for  %s", name);
	init_t xinit = init;
	(xinit)();
}

int main(int argc, char **argv) {
	printf("MCP initializing");

	int power = gpio_configure(GPIO_DAC_POWER, 1, 0, -1);
	printf("DAC power is %s", power ? "ON" : "OFF");

	gpio_configure(GPIO_DAC_MCLK, 3, 0, -1);
	gpio_print(GPIO_DAC_MCLK);

//	gpio_x();

	if (power)
		gpio_set(GPIO_DAC_POWER, 0);
	else
		gpio_set(GPIO_DAC_POWER, 1);
}
