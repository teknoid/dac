#define XLOG_NONE					0
#define XLOG_STDOUT					1
#define XLOG_SYSLOG					2
#define XLOG_FILE					3

#define SPACEMASK32					0x01010101
#define SPACEMASK64					0x0101010101010101

#define ARRAY_SIZE(x)				(sizeof(x) / sizeof(x[0]))

#define ZEROP(x)					memset((void*) x, 0, sizeof(*x))
#define ZERO(x)						memset((void*) &x, 0, sizeof(x))

#define SWAP16(x)					x = (((x << 8) & 0xff00) | ((x >> 8) & 0x00ff))
#define SWAP32(x)					x = (((x << 16) & 0xffff0000) | ((x >> 16) & 0x0000ffff))

#define FLOAT10(x)					((float) x / 10.0)
#define FLOAT60(x)					((float) x / 60.0)
#define FLOAT100(x)					((float) x / 100.0)

#define msleep(x)					usleep(x * 1000)

#define LINEBUF						256

#define BYTE2BIN_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE2BIN(x)  \
  ((x) & 0x80 ? '1' : '0'), \
  ((x) & 0x40 ? '1' : '0'), \
  ((x) & 0x20 ? '1' : '0'), \
  ((x) & 0x10 ? '1' : '0'), \
  ((x) & 0x08 ? '1' : '0'), \
  ((x) & 0x04 ? '1' : '0'), \
  ((x) & 0x02 ? '1' : '0'), \
  ((x) & 0x01 ? '1' : '0')

#define BYTE2BIN_PATTERN16 "%c%c%c%c%c%c%c%c %c%c%c%c%c%c%c%c"
#define BYTE2BIN16(x)  \
  ((x) & 0x8000 ? '1' : '0'), \
  ((x) & 0x4000 ? '1' : '0'), \
  ((x) & 0x2000 ? '1' : '0'), \
  ((x) & 0x1000 ? '1' : '0'), \
  ((x) & 0x0800 ? '1' : '0'), \
  ((x) & 0x0400 ? '1' : '0'), \
  ((x) & 0x0200 ? '1' : '0'), \
  ((x) & 0x0100 ? '1' : '0'), \
  ((x) & 0x0080 ? '1' : '0'), \
  ((x) & 0x0040 ? '1' : '0'), \
  ((x) & 0x0020 ? '1' : '0'), \
  ((x) & 0x0010 ? '1' : '0'), \
  ((x) & 0x0008 ? '1' : '0'), \
  ((x) & 0x0004 ? '1' : '0'), \
  ((x) & 0x0002 ? '1' : '0'), \
  ((x) & 0x0001 ? '1' : '0')

#define PROFILING_START \
		struct timespec ts_profiling; \
		int64_t ts_profiling_start, ts_profiling_end, ts_profiling_ns, ts_profiling_us, ts_profiling_ms; \
		clock_gettime(CLOCK_REALTIME, &ts_profiling); \
		ts_profiling_start = ts_profiling.tv_nsec;

#define PROFILING_RESTART \
		clock_gettime(CLOCK_REALTIME, &ts_profiling); \
		ts_profiling_start = ts_profiling.tv_nsec;

#define PROFILING_LOG(s) \
		clock_gettime(CLOCK_REALTIME, &ts_profiling); \
		ts_profiling_end = ts_profiling.tv_nsec; \
		ts_profiling_ns = ts_profiling_end - ts_profiling_start; \
		ts_profiling_us = ts_profiling_ns / 1000; \
		ts_profiling_ms = ts_profiling_us / 1000; \
		if (ts_profiling_ms) \
			xlog("%s profiling in %dms", s, ts_profiling_ms); \
		else if (ts_profiling_us) \
			xlog("%s profiling in %dus", s, ts_profiling_us); \
		else \
			xlog("%s profiling in %dns", s, ts_profiling_ns);

void set_xlog(int output);
void set_debug(int debug);
void xlog_close();
void xlog(const char *format, ...);
void xdebug(const char *format, ...);
int xerr(const char *format, ...);
int xerrr(int ret, const char *format, ...);

void xlogl_start(char *line, const char *s);
void xlogl_bits(char *line, const char *name, int bits);
void xlogl_bits16(char *line, const char *name, int bits);
void xlogl_float(char *line, const char *name, float value);
void xlogl_float_b(char *line, const char *name, float value);
void xlogl_float_c(char *line, int invers, const char *name, float value);
void xlogl_int(char *line, const char *name, int value);
void xlogl_int_r(char *line, const char *name, int value);
void xlogl_int_y(char *line, const char *name, int value);
void xlogl_int_g(char *line, const char *name, int value);
void xlogl_int_b(char *line, const char *name, int value);
void xlogl_int_B(char *line, const char *name, int value);
void xlogl_int_noise(char *line, int noise, int invers, const char *name, int value);
void xlogl_end(char *line, size_t len, const char *s);

int elevate_realtime(int cpu);

// WARNING strings are malloc'd - take care of freeing them after usage
char* printbits64(uint64_t code, uint64_t spacemask);
char* printbits32(uint32_t code, uint32_t spacemask);
char* printbits(uint8_t value);

void hexdump(char *desc, void *addr, int len);

int starts_with(const char *pre, const char *str, unsigned int strsize);
int ends_with(const char *post, const char *str, unsigned int strsize);
char* make_string(const char *c, size_t t);

void create_sysfslike(char *dir, char *fname, char *fvalue, const char *fmt, ...);

char* devinput_keyname(unsigned int key);
int devinput_find_key(const char *name);

uint64_t mac2uint64(const char *mac);

const char* resolve_ip(const char *hostname);

int round10(int n);
int round100(int n);

int maximum(int, ...);
int average_non_zero(int array[], size_t size);

void append_timeframe(char *message, int sec);

int load_blob(const char *filename, void *data, size_t size);
int store_blob(const char *filename, void *data, size_t size);
int store_blob_offset(const char *filename, void *data, size_t rsize, int count, int offset);

void aggregate(int *target, int *table, int cols, int rows);
void cumulate(int *target, int *table, int cols, int rows);
void store_csv_header(const char *header, const char *filename);
void store_array_csv(int array[], int size, const char *header, const char *filename);
void store_table_csv(int *table, int cols, int rows, const char *header, const char *filename);
void append_table_csv(int *table, int cols, int rows, int offset, const char *filename);
void dump_table(int *table, int cols, int rows, int highlight_row, const char *title, const char *header);
void dump_struct(int *values, int size, const char *idx, const char *title);

void store_struct_json(int *values, int size, const char *header, const char *filename);
