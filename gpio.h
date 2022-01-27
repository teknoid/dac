int gpio_configure(const char *name, int function, int trigger, int initial);

int gpio_get(const char *name);

int gpio_toggle(const char *name);

void gpio_set(const char *name, int value);

void gpio_print(const char *name);

int gpio_init(void);

void gpio_close(void);
