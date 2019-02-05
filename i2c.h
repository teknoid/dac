#include <stdint.h>

#ifndef I2C
#define I2C		"/dev/i2c-0"
#endif

int i2c_write(uint8_t addr, uint8_t reg, char value);
int i2c_read(uint8_t addr, uint8_t reg, char *val);
int i2c_set_bit(uint8_t addr, uint8_t reg, int n);
int i2c_clear_bit(uint8_t addr, uint8_t reg, int n);
void i2c_dump_reg(uint8_t addr, uint8_t reg);
int i2c_init(char *device);
void i2c_close();
