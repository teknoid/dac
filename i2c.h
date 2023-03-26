uint8_t i2c_put(int, uint8_t, uint8_t);
uint8_t i2c_get(int, uint8_t);
int i2c_read(int, uint8_t, uint8_t, uint8_t*);
int i2c_write(int, uint8_t, uint8_t, uint8_t);
int i2c_read_bits(int, uint8_t, uint8_t, uint8_t*, uint8_t);
int i2c_write_bits(int, uint8_t, uint8_t, uint8_t, uint8_t);
int i2c_set_bit(int, uint8_t, uint8_t, int);
int i2c_clear_bit(int, uint8_t, uint8_t, int);
void i2c_dump_reg(int, uint8_t, uint8_t);
