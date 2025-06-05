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

#define SYSFSLIKE				0
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

	sensors->bh1750_prc = (sqrt(sensors->bh1750_raw) * 100) / UINT8_MAX;
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

static void write_json() {
	FILE *fp = fopen(RUN SLASH SENSORS_JSON, "wt");
	if (fp == NULL)
		return;

	fprintf(fp, "\{");
	fprintf(fp, "\"temp_in\":%.1f,", sensors->htu21_temp);
	fprintf(fp, "\"humi_in\":%.1f,", sensors->htu21_humi);
	fprintf(fp, "\"temp_out\":%.1f,", sensors->sht31_temp);
	fprintf(fp, "\"humi_out\":%.1f,", sensors->sht31_humi);
	fprintf(fp, "\"dewpoint\":%.1f,", sensors->sht31_dew);
	fprintf(fp, "\"lumi\":%d", sensors->bh1750_lux);
	fprintf(fp, "}");
	fflush(fp);
	fclose(fp);
}

static void publish_sensor(const char *sensor, const char *name, const char *value) {
#ifdef PICAM
	char subtopic[64];
	snprintf(subtopic, sizeof(subtopic), "%s/%s/%s", "sensor", sensor, name);
	publish(subtopic, value);
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

	snprintf(cvalue, 4, "%u", sensors->bh1750_prc);
	publish_sensor(BH1750, "lum_percent", cvalue);

	snprintf(cvalue, 5, "%0.1f", sensors->bmp085_temp);
	publish_sensor(BMP085, "temp", cvalue);

	snprintf(cvalue, 8, "%0.1f", sensors->bmp085_baro);
	publish_sensor(BMP085, "baro", cvalue);
}

static void write_sysfslike() {
	char cvalue[8];

	snprintf(cvalue, 6, "%u", sensors->bh1750_raw);
	create_sysfslike(RAM, "lum_raw", cvalue, "%s", BH1750);

	snprintf(cvalue, 6, "%u", sensors->bh1750_raw2);
	create_sysfslike(RAM, "lum_raw2", cvalue, "%s", BH1750);

	snprintf(cvalue, 6, "%u", sensors->bh1750_lux);
	create_sysfslike(RAM, "lum_lux", cvalue, "%s", BH1750);

	snprintf(cvalue, 4, "%u", sensors->bh1750_prc);
	create_sysfslike(RAM, "lum_percent", cvalue, "%s", BH1750);

	snprintf(cvalue, 5, "%0.1f", sensors->bmp085_temp);
	create_sysfslike(RAM, "temp", cvalue, "%s", BMP085);

	snprintf(cvalue, 8, "%0.1f", sensors->bmp085_baro);
	create_sysfslike(RAM, "baro", cvalue, "%s", BMP085);
}

static void loop() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	while (1) {
		read_bh1750();
		read_bmp085();

		publish_sensors();
		write_json();

		if (SYSFSLIKE)
			write_sysfslike();

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
		xlog("BH1750 raw  %d", sensors->bh1750_raw);
		xlog("BH1750 raw2 %d", sensors->bh1750_raw2);
		xlog("BH1750 lux  %d lx", sensors->bh1750_lux);
		xlog("BH1750 prc  %d %%", sensors->bh1750_prc);

		xlog("BMP085 temp %d (raw)", sensors->bmp085_temp_raw);
		xlog("BMP085 baro %d (raw)", sensors->bmp085_baro_raw);

		xlog("BMP085 temp %0.1f °C", sensors->bmp085_temp);
		xlog("BMP085 baro %0.1f hPa", sensors->bmp085_baro);

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
MCP_REGISTER(sensors, 2, &init, &stop, &loop);
#endif
