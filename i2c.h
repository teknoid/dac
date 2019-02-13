#include <stdint.h>

#ifndef I2C
#define I2C		"/dev/i2c-0"
#endif

int i2c_read(uint8_t, uint8_t, char *);
int i2c_write(uint8_t, uint8_t, char);
int i2c_read_bits(uint8_t, uint8_t, char *, uint8_t);
int i2c_write_bits(uint8_t, uint8_t, char, uint8_t);
int i2c_set_bit(uint8_t, uint8_t, int);
int i2c_clear_bit(uint8_t, uint8_t, int);
void i2c_dump_reg(uint8_t, uint8_t);
int i2c_init(char *);
void i2c_close();
