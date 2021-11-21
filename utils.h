#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define ZERO(a) memset(a, 0, sizeof(*a));

#define msleep(x) usleep(x*1000)

unsigned long _micros();

int init_micros();

void delay_micros(unsigned int us);

int elevate_realtime(int cpu);

char* printBits(char value);

void xlog(char *format, ...);
void xlog_close(void);

int startsWith(const char *pre, const char *str);

void hexDump(char *desc, void *addr, int len);

char* devinput_keyname(unsigned int key);
int devinput_find_key(const char *name);
