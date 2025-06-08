#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "utils.h"
#include "gpio.h"
#include "mcp.h"

#define GPIO_DAC_POWER		"PG11"
#define GPIO_DAC_MCLK		"PA6"

typedef int (*init_t)();

// gpio-bcm2835.c needs mcp_register()
void mcp_register(const char *name, const int prio, const init_t init, const stop_t stop, const loop_t loop) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);
	xlog("call init() + loop() for  %s", name);
	(init)();
}

int main(int argc, char **argv) {
	xlog("SWITCH initializing");

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
