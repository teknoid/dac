#include <stdint.h>

#define XLOG_STDOUT					0
#define XLOG_SYSLOG					1
#define XLOG_FILE					2

#define SPACEMASK					0x01010101

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define ZERO(a) memset(a, 0, sizeof(*a));

#define msleep(x) usleep(x*1000)

int elevate_realtime(int cpu);

void xlog_init(int, const char *filename);
void xlog_close(void);
void xlog(const char *format, ...);
int xerr(const char *format, ...);

char* printbits64(uint64_t code, uint64_t spacemask);

char* printbits(uint32_t code, uint32_t spacemask);

void hexdump(char *desc, void *addr, int len);

int starts_with(const char *pre, const char *str);

void create_sysfslike(char *dir, char *fname, char *fvalue, const char *fmt, ...);

char* devinput_keyname(unsigned int key);
int devinput_find_key(const char *name);
