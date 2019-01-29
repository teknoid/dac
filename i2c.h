#ifndef I2C
#define I2C		"/dev/i2c-0"
#endif

int i2c_write(char addr, char reg, char value);
int i2c_read(char addr, char reg, char *val);
int i2c_set_bit(char addr, char reg, int n);
int i2c_clear_bit(char addr, char reg, int n);
void i2c_dump_reg(char addr, char reg);
int i2c_init(char *device);
void i2c_close();
