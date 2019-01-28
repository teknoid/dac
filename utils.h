void xlog(char *format, ...);
void xlog_close(void);
int startsWith(const char *pre, const char *str);
char *printBits(char value);
void hexDump(char *desc, void *addr, int len);
char *devinput_keyname(unsigned int key);
int devinput_find_key(const char *name);
