#define SENSORS_JSON		"sensors.json"

#define BH1750				"BH1750"
#define BH1750_ADDR			0x23
#define BH1750_POWERDOWN	0x00
#define BH1750_POWERON		0x01
#define BH1750_RESET		0x07
#define BH1750_CHM			0x10
#define BH1750_CHM2			0x11
#define BH1750_CLM			0x13
#define BH1750_OTHM			0x20
#define BH1750_OTHM2		0x21
#define BH1750_OTLM			0x23

#define BMP085				"BMP085"
#define BMP085_ADDR			0x77
#define BMP085_OVERSAMPLE	3

typedef struct sensors_t {
	// BH1750 luminousity
	uint16_t bh1750_raw;
	uint16_t bh1750_raw2;
	uint8_t bh1750_prc;
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

} sensors_t;

extern sensors_t *sensors;

void sensors_bh1750_calc_mean();
