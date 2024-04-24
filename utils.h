#define XLOG_STDOUT					0
#define XLOG_SYSLOG					1
#define XLOG_FILE					2

#define SPACEMASK32					0x01010101
#define SPACEMASK64					0x0101010101010101

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

#define ZERO(x) memset(x, 0, sizeof(*x));

#define SWAP(x) ((x<<8) & 0xFF00) | ((x>>8) & 0xFF)

#define msleep(x) usleep(x * 1000)

#define LINEBUF 256

void set_xlog(int output);
void set_debug(int debug);
void xlog_close();
void xlog(const char *format, ...);
void xdebug(const char *format, ...);
int xerr(const char *format, ...);
int xerrr(int ret, const char *format, ...);

void xlogl_start(char *line, const char *s);
void xlogl_int(char *line, int colored, int invers, const char *name, int value);
void xlogl_end(char *line, size_t len, const char *s);
void xlogl_int_r(char *line, const char *name, int value);
void xlogl_int_y(char *line, const char *name, int value);
void xlogl_int_g(char *line, const char *name, int value);
void xlogl_int_b(char *line, const char *name, int value);
void xlogl_int_B(char *line, const char *name, int value);

int elevate_realtime(int cpu);

// WARNING strings are malloc'd - take care of freeing them after usage
char* printbits64(uint64_t code, uint64_t spacemask);
char* printbits32(uint32_t code, uint32_t spacemask);
char* printbits(uint8_t value);

void hexdump(char *desc, void *addr, int len);

int starts_with(const char *pre, const char *str, unsigned int strsize);
int ends_with(const char *post, const char *str, unsigned int strsize);

void create_sysfslike(char *dir, char *fname, char *fvalue, const char *fmt, ...);

char* devinput_keyname(unsigned int key);
int devinput_find_key(const char *name);

uint64_t mac2uint64(char *mac);

const char* resolve_ip(const char *hostname);

int round10(int n);
int round100(int n);

int maximum(int, ...);
