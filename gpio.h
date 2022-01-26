void gpio_print(const char *name);

void gpio_func(const char *name, int function, int trigger);

void gpio_set(const char *name, int value);

void gpio_toggle(const char *name);

int gpio_get(const char *name);

int gpio_init(void);

void gpio_close(void);
