#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include "sensors.h"
#include "utils.h"
#include "mqtt.h"
#include "i2c.h"
#include "mcp.h"

#ifndef I2C
#define I2C						"/dev/i2c-0"
#endif

#define WRITE_JSON				1
#define WRITE_SYSFSLIKE			0

#define JSON_KEY(k)				"\"" k "\""
#define JSON_INT(k)				"\"" k "\":%d"
#define JSON_FLOAT(k)			"\"" k "\":%.1f"
#define COMMA					", "

#define MEAN					10
static unsigned int bh1750_lux_mean[MEAN];
static int mean;

static int i2cfd = 0;

sensors_t sensors_local, *sensors = &sensors_local;

// bisher gefühlt bei 100 Lux (19:55)
// Straßenlampe Eisenstraße 0x40 = XX Lux
// Straßenlampe Wiesenstraße bei 0x08 = 6 Lux (20:15)
// Kamera aus bei 0x02 = 1 Lux (20:22)
// 0x00 (20:25) aber noch deutlich hell am Westhorizont

// https://forums.raspberrypi.com/viewtopic.php?t=38023
static void read_bh1750() {
	if (!i2cfd)
		return;

	// powerup
	i2c_put(i2cfd, BH1750_ADDR, BH1750_POWERON);

	// continuous high mode (resolution 1lx)
	i2c_put(i2cfd, BH1750_ADDR, BH1750_CHM);
	msleep(180);
	i2c_get_int(i2cfd, BH1750_ADDR, &sensors->bh1750_raw);

	// continuous high mode 2 (resolution 0.5lx)
	i2c_put(i2cfd, BH1750_ADDR, BH1750_CHM2);
	msleep(180);
	i2c_get_int(i2cfd, BH1750_ADDR, &sensors->bh1750_raw2);

	// sleep
	i2c_put(i2cfd, BH1750_ADDR, BH1750_POWERDOWN);

	if (sensors->bh1750_raw2 == UINT16_MAX)
		sensors->bh1750_lux = sensors->bh1750_raw / 1.2;
	else
		sensors->bh1750_lux = sensors->bh1750_raw2 / 2.4;

	sensors_bh1750_calc_mean();
}

// https://forums.raspberrypi.com/viewtopic.php?t=16968
static void read_bmp085() {
	if (!i2cfd)
		return;

	int16_t ac1;
	int16_t ac2;
	int16_t ac3;
	uint16_t ac4;
	uint16_t ac5;
	uint16_t ac6;
	int16_t b1;
	int16_t b2;
	int16_t mb;
	int16_t mc;
	int16_t md;

	// read calibration data
	i2c_read_int(i2cfd, BMP085_ADDR, 0xAA, (uint16_t*) &ac1);
	i2c_read_int(i2cfd, BMP085_ADDR, 0xAC, (uint16_t*) &ac2);
	i2c_read_int(i2cfd, BMP085_ADDR, 0xAE, (uint16_t*) &ac3);
	i2c_read_int(i2cfd, BMP085_ADDR, 0xB0, &ac4);
	i2c_read_int(i2cfd, BMP085_ADDR, 0xB2, &ac5);
	i2c_read_int(i2cfd, BMP085_ADDR, 0xB4, &ac6);
	i2c_read_int(i2cfd, BMP085_ADDR, 0xB6, (uint16_t*) &b1);
	i2c_read_int(i2cfd, BMP085_ADDR, 0xB8, (uint16_t*) &b2);
	i2c_read_int(i2cfd, BMP085_ADDR, 0xBA, (uint16_t*) &mb);
	i2c_read_int(i2cfd, BMP085_ADDR, 0xBC, (uint16_t*) &mc);
	i2c_read_int(i2cfd, BMP085_ADDR, 0xBE, (uint16_t*) &md);

	// temperature
	i2c_write(i2cfd, BMP085_ADDR, 0xF4, 0x2E);
	msleep(5);
	i2c_read_int(i2cfd, BMP085_ADDR, 0xF6, &sensors->bmp085_temp_raw);
	int x1 = (((int) sensors->bmp085_temp_raw - (int) ac6) * (int) ac5) >> 15;
	int x2 = ((int) mc << 11) / (x1 + md);
	int b5 = x1 + x2;
	sensors->bmp085_temp = ((b5 + 8) >> 4) / 10.0;

	// pressure
	uint8_t buf[3];
	i2c_write(i2cfd, BMP085_ADDR, 0xF4, 0x34 + (BMP085_OVERSAMPLE << 6));
	msleep(2 + (3 << BMP085_OVERSAMPLE));
	i2c_read_block(i2cfd, BMP085_ADDR, 0xF6, buf, 3);
	sensors->bmp085_baro_raw = ((buf[0] << 16) | (buf[1] << 8) | buf[2]) >> (8 - BMP085_OVERSAMPLE);
	int b6 = b5 - 4000;
	x1 = (b2 * (b6 * b6) >> 12) >> 11;
	x2 = (ac2 * b6) >> 11;
	int x3 = x1 + x2;
	int b3 = (((((int) ac1) * 4 + x3) << BMP085_OVERSAMPLE) + 2) >> 2;
	x1 = (ac3 * b6) >> 13;
	x2 = (b1 * ((b6 * b6) >> 12)) >> 16;
	x3 = ((x1 + x2) + 2) >> 2;
	unsigned int b4 = (ac4 * (unsigned int) (x3 + 32768)) >> 15;
	unsigned int b7 = ((unsigned int) (sensors->bmp085_baro_raw - b3) * (50000 >> BMP085_OVERSAMPLE));
	int p;
	if (b7 < 0x80000000)
		p = (b7 << 1) / b4;
	else
		p = (b7 / b4) << 1;
	x1 = (p >> 8) * (p >> 8);
	x1 = (x1 * 3038) >> 16;
	x2 = (-7357 * p) >> 16;
	p += (x1 + x2 + 3791) >> 4;
	sensors->bmp085_baro = p / 100.0;
}

static void publish_sensors_tasmotalike() {
	char hostname[32], subtopic[64], value[BUFSIZE], v[64];
	gethostname(hostname, 32);

	// snprintf(subtopic, sizeof(subtopic), "tele/%s/SENSOR", hostname);
	// TODO generate id from name or mac address
	snprintf(subtopic, sizeof(subtopic), "tele/5213d6/SENSOR");
	snprintf(value, 64, "{");

	snprintf(v, 64, JSON_KEY(BH1750) ":{\"Illuminance\":%d}" COMMA, sensors->bh1750_raw);
	strncat(value, v, 64);

	snprintf(v, 64, JSON_KEY(BMP085) ":{\"Temperature\":%.1f, \"Pressure\":%.1f}", sensors->bmp085_temp, sensors->bmp085_baro);
	strncat(value, v, 64);

	snprintf(v, 64, "}");
	strncat(value, v, 64);

#ifdef PICAM
	publish(subtopic, value, 1);
#endif
}

static void publish_sensor(const char *sensor, const char *name, const char *value) {
	char hostname[32], subtopic[64];
	gethostname(hostname, 32);
	snprintf(subtopic, sizeof(subtopic), "%s/%s/%s", hostname, sensor, name);
#ifdef PICAM
	publish(subtopic, value, 0);
#endif
}

static void publish_sensors() {
	char cvalue[8];

	snprintf(cvalue, 6, "%u", sensors->bh1750_raw);
	publish_sensor(BH1750, "lum_raw", cvalue);

	snprintf(cvalue, 6, "%u", sensors->bh1750_raw2);
	publish_sensor(BH1750, "lum_raw2", cvalue);

	snprintf(cvalue, 6, "%u", sensors->bh1750_lux);
	publish_sensor(BH1750, "lum_lux", cvalue);

	snprintf(cvalue, 5, "%0.1f", sensors->bmp085_temp);
	publish_sensor(BMP085, "temp", cvalue);

	snprintf(cvalue, 8, "%0.1f", sensors->bmp085_baro);
	publish_sensor(BMP085, "baro", cvalue);

	snprintf(cvalue, 5, "%0.1f", sensors->bmp280_temp);
	publish_sensor(BMP280, "temp", cvalue);

	snprintf(cvalue, 8, "%0.1f", sensors->bmp280_baro);
	publish_sensor(BMP280, "baro", cvalue);
}

static void write_sensors_sysfslike() {
	char cvalue[8];

	snprintf(cvalue, 6, "%u", sensors->bh1750_raw);
	create_sysfslike(RAM, "lum_raw", cvalue, "%s", BH1750);

	snprintf(cvalue, 6, "%u", sensors->bh1750_raw2);
	create_sysfslike(RAM, "lum_raw2", cvalue, "%s", BH1750);

	snprintf(cvalue, 6, "%u", sensors->bh1750_lux);
	create_sysfslike(RAM, "lum_lux", cvalue, "%s", BH1750);

	snprintf(cvalue, 5, "%0.1f", sensors->bmp085_temp);
	create_sysfslike(RAM, "temp", cvalue, "%s", BMP085);

	snprintf(cvalue, 8, "%0.1f", sensors->bmp085_baro);
	create_sysfslike(RAM, "baro", cvalue, "%s", BMP085);

	snprintf(cvalue, 5, "%0.1f", sensors->bmp280_temp);
	create_sysfslike(RAM, "temp", cvalue, "%s", BMP280);

	snprintf(cvalue, 8, "%0.1f", sensors->bmp280_baro);
	create_sysfslike(RAM, "baro", cvalue, "%s", BMP280);
}

static void write_sensors_json() {
	FILE *fp = fopen(RUN SLASH SENSORS_JSON, "wt");
	if (fp == NULL)
		return;

	fprintf(fp, "\{");
	fprintf(fp, JSON_FLOAT(BMP085 "_TEMP") COMMA, sensors->bmp085_temp);
	fprintf(fp, JSON_FLOAT(BMP085 "_BARO") COMMA, sensors->bmp085_baro);
	fprintf(fp, JSON_FLOAT(BMP280 "_TEMP") COMMA, sensors->bmp280_temp);
	fprintf(fp, JSON_FLOAT(BMP280 "_BARO") COMMA, sensors->bmp280_baro);
	fprintf(fp, JSON_FLOAT(HTU21 "_TEMP") COMMA, sensors->htu21_temp);
	fprintf(fp, JSON_FLOAT(HTU21 "_HUMI") COMMA, sensors->htu21_humi);
	fprintf(fp, JSON_FLOAT(SHT31 "_TEMP") COMMA, sensors->sht31_temp);
	fprintf(fp, JSON_FLOAT(SHT31 "_HUMI") COMMA, sensors->sht31_humi);
	fprintf(fp, JSON_FLOAT(SHT31 "_DEW") COMMA, sensors->sht31_dew);
	fprintf(fp, JSON_INT(BH1750 "_LUX"), sensors->bh1750_lux);
	fprintf(fp, "}");
	fflush(fp);
	fclose(fp);
}

static void loop() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

#ifndef PICAM
	// wait till mqtt received essential sensor data
	// TODO funktioniert nicht
	int retry = 100;
	while (--retry && sensors->sht31_temp > 1000 && sensors->htu21_temp > 1000 && sensors->bh1750_lux == UINT16_MAX)
		msleep(100);
	if (retry)
		xdebug("SENSORS ok: retry=%d sht31=%.1f htu21=%.1f bh1750=%d", retry, sensors->sht31_temp, sensors->htu21_temp, sensors->bh1750_raw);
	else
		xdebug("SENSORS Warning! MQTT sensor data incomplete: sht31=%.1f htu21=%.1f bh1750=%d", sensors->sht31_temp, sensors->htu21_temp, sensors->bh1750_lux);
#endif

	while (1) {
		read_bh1750();
		read_bmp085();

		publish_sensors_tasmotalike();
		publish_sensors();

		if (WRITE_JSON)
			write_sensors_json();

		if (WRITE_SYSFSLIKE)
			write_sensors_sysfslike();

		sleep(60);
	}
}

static int init() {
#ifdef PICAM
	i2cfd = open(I2C, O_RDWR);
	if (i2cfd < 0)
		return xerr("I2C BUS error");
#endif

	// clear average value buffer
	ZERO(bh1750_lux_mean);
	mean = 0;

	// initialize sensor data
	sensors->bh1750_lux = UINT16_MAX;
	sensors->bh1750_lux_mean = UINT16_MAX;
	sensors->bmp085_temp = UINT16_MAX;
	sensors->bmp085_baro = UINT16_MAX;
	sensors->bmp280_temp = UINT16_MAX;
	sensors->bmp280_baro = UINT16_MAX;
	sensors->sht31_humi = UINT16_MAX;
	sensors->sht31_temp = UINT16_MAX;
	sensors->sht31_dew = UINT16_MAX;
	sensors->htu21_humi = UINT16_MAX;
	sensors->htu21_temp = UINT16_MAX;
	sensors->htu21_dew = UINT16_MAX;
	sensors->ml8511_uv = UINT16_MAX;

	return 0;
}

static void stop() {
	if (i2cfd > 0)
		close(i2cfd);
}

void sensors_bh1750_calc_mean() {
	bh1750_lux_mean[mean++] = sensors->bh1750_lux;
	if (mean == MEAN)
		mean = 0;

	unsigned long sum = 0;
	for (int i = 0; i < MEAN; i++)
		sum += bh1750_lux_mean[i];

	sensors->bh1750_lux_mean = sum / MEAN;
}

int sensor_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	init();
	sleep(1);

	while (1) {
		xlog(BH1750" raw  %d", sensors->bh1750_raw);
		xlog(BH1750" raw2 %d", sensors->bh1750_raw2);
		xlog(BH1750" lux  %d lx", sensors->bh1750_lux);

		xlog(BMP085" temp %d (raw)", sensors->bmp085_temp_raw);
		xlog(BMP085" baro %d (raw)", sensors->bmp085_baro_raw);

		xlog(BMP085" temp %0.1f °C", sensors->bmp085_temp);
		xlog(BMP085" baro %0.1f hPa", sensors->bmp085_baro);

		xlog(BMP280" temp %0.1f °C", sensors->bmp280_temp);
		xlog(BMP280" baro %0.1f hPa", sensors->bmp280_baro);

		sleep(10);
	}

	stop();
	return 0;
}
#ifdef SENSORS_MAIN
int main(int argc, char **argv) {
	return sensor_main(argc, argv);
}
#else
MCP_REGISTER(sensors, 5, &init, &stop, &loop);
#endif
