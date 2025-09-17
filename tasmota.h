#include "tasmota-devices.h"

typedef struct tasmota_config_t {
	const unsigned int id;
	const unsigned int relay;
	const unsigned int t1;
	const unsigned int t1b;
	const unsigned int t2;
	const unsigned int t2b;
	const unsigned int t3;
	const unsigned int t3b;
	const unsigned int t4;
	const unsigned int t4b;
	const unsigned int timer;
} tasmota_config_t;

typedef struct _tasmota tasmota_t;

struct _tasmota {
	unsigned int id;
	unsigned int online;
	char *name;
	tasmota_t *next;

	// actual event
	unsigned int relay;
	unsigned int power;

	// saved power states
	unsigned int relay1;
	unsigned int relay2;
	unsigned int relay3;
	unsigned int relay4;

	// shutter position
	unsigned int position;
	unsigned int timer;

	// synchronize with struct sensors_t in sensors.h

	// DS18B20 temperature sensor
	uint32_t ds18b20_id;
	float ds18b20_temp;

	// BH1750 luminousity
	uint16_t bh1750_raw;
	uint16_t bh1750_raw2;
	uint16_t bh1750_lux;
	uint16_t bh1750_lux_mean;

	// BMP085 temperature + barometric pressure
	float bmp085_temp;
	uint16_t bmp085_temp_raw;
	float bmp085_baro;
	uint32_t bmp085_baro_raw;

	// BMP280 temperature + barometric pressure
	float bmp280_temp;
	uint16_t bmp280_temp_raw;
	float bmp280_baro;
	uint32_t bmp280_baro_raw;

	// SHT31 temperature + humidity
	float sht31_humi;
	float sht31_temp;
	float sht31_dew;

	// HTU21 temperature + humidity
	float htu21_humi;
	float htu21_temp;
	float htu21_dew;

	// ML8511 UV
	uint16_t ml8511_uv;

	// GP8403 I2C DAC
	int gp8403_vc0;
	int gp8403_vc1;
	int gp8403_pc0;
	int gp8403_pc1;
};

int openbeken_color(unsigned int id, int r, int g, int b);
int openbeken_dimmer(unsigned int id, int d);
int openbeken_set(unsigned int id, int channel, int value);

int tasmota_power(unsigned int id, int relay, int power);
int tasmota_power_on(unsigned int id);
int tasmota_power_off(unsigned int id);
int tasmota_power_ask(unsigned int id, int relay);
int tasmota_status_ask(unsigned int id, int status);

int tasmota_shutter(unsigned int, unsigned int);

int tasmota_dispatch(const char *topic, uint16_t tsize, const char *message, size_t msize);

tasmota_t* tasmota_get_by_id(unsigned int id);
tasmota_t* tasmota_get_by_name(const char *name);
