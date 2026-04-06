#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include "tasmota.h"
#include "sensors.h"
#include "utils.h"
#include "mqtt.h"
#include "i2c.h"
#include "mcp.h"

#ifndef TVOC
#define TVOC					199
#endif

#ifndef TEMP_IN
#define TEMP_IN					22.0
#endif

#ifndef TEMP_OUT
#define TEMP_OUT				15.0
#endif

#ifndef HUMI
#define HUMI					33
#endif

#ifndef LUMI
#define LUMI					6666
#endif

#ifndef I2C
#define I2C						"/dev/i2c-0"
#endif

#define WRITE_JSON				1
#define WRITE_SYSFSLIKE			0

#define JSON_KEY(k)				"\"" k "\""
#define JSON_INT(k)				"\"" k "\":%d"
#define JSON_FLOAT(k)			"\"" k "\":%.1f"
#define COMMA					", "

// hexdump -v -e '15 "%10d ""\n"' /var/lib/mcp/sensors.bin
#define SENSORS_FILE			"sensors.bin"

#define MEAN					10
static unsigned int bh1750_lux_mean[MEAN];
static int mean;

static int i2cfd = 0;

static sensors_t sensors[5];

// global sensors pointer, actual and 0, 6, 12, 18 o'clock
sensors_t *sensor = &sensors[0];
sensors_t *sensor0 = &sensors[1], *sensor6 = &sensors[2], *sensor12 = &sensors[3], *sensor18 = &sensors[4];

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
	i2c_get_int(i2cfd, BH1750_ADDR, &sensor->bh1750_raw);

	// continuous high mode 2 (resolution 0.5lx)
	i2c_put(i2cfd, BH1750_ADDR, BH1750_CHM2);
	msleep(180);
	i2c_get_int(i2cfd, BH1750_ADDR, &sensor->bh1750_raw2);

	// sleep
	i2c_put(i2cfd, BH1750_ADDR, BH1750_POWERDOWN);

	if (sensor->bh1750_raw2 == UINT16_MAX)
		sensor->bh1750_lux = sensor->bh1750_raw / 1.2;
	else
		sensor->bh1750_lux = sensor->bh1750_raw2 / 2.4;
}

static void mean_bh1750() {
	bh1750_lux_mean[mean++] = sensor->bh1750_lux;
	if (mean == MEAN)
		mean = 0;

	unsigned long sum = 0;
	for (int i = 0; i < MEAN; i++)
		sum += bh1750_lux_mean[i];

	sensor->bh1750_lux_mean = sum / MEAN;
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
	i2c_read_int(i2cfd, BMP085_ADDR, 0xF6, &sensor->bmp085_temp_raw);
	int x1 = (((int) sensor->bmp085_temp_raw - (int) ac6) * (int) ac5) >> 15;
	int x2 = ((int) mc << 11) / (x1 + md);
	int b5 = x1 + x2;
	sensor->bmp085_temp = ((b5 + 8) >> 4) / 10.0;

	// pressure
	uint8_t buf[3];
	i2c_write(i2cfd, BMP085_ADDR, 0xF4, 0x34 + (BMP085_OVERSAMPLE << 6));
	msleep(2 + (3 << BMP085_OVERSAMPLE));
	i2c_read_block(i2cfd, BMP085_ADDR, 0xF6, buf, 3);
	sensor->bmp085_baro_raw = ((buf[0] << 16) | (buf[1] << 8) | buf[2]) >> (8 - BMP085_OVERSAMPLE);
	int b6 = b5 - 4000;
	x1 = (b2 * (b6 * b6) >> 12) >> 11;
	x2 = (ac2 * b6) >> 11;
	int x3 = x1 + x2;
	int b3 = (((((int) ac1) * 4 + x3) << BMP085_OVERSAMPLE) + 2) >> 2;
	x1 = (ac3 * b6) >> 13;
	x2 = (b1 * ((b6 * b6) >> 12)) >> 16;
	x3 = ((x1 + x2) + 2) >> 2;
	unsigned int b4 = (ac4 * (unsigned int) (x3 + 32768)) >> 15;
	unsigned int b7 = ((unsigned int) (sensor->bmp085_baro_raw - b3) * (50000 >> BMP085_OVERSAMPLE));
	int p;
	if (b7 < 0x80000000)
		p = (b7 << 1) / b4;
	else
		p = (b7 / b4) << 1;
	x1 = (p >> 8) * (p >> 8);
	x1 = (x1 * 3038) >> 16;
	x2 = (-7357 * p) >> 16;
	p += (x1 + x2 + 3791) >> 4;
	sensor->bmp085_baro = p / 100.0;
}

static void publish_sensors_tasmotalike() {
	char hostname[32], subtopic[64], value[BUFSIZE], v[64];
	gethostname(hostname, 32);

	// snprintf(subtopic, sizeof(subtopic), "tele/%s/SENSOR", hostname);
	// TODO generate id from name or mac address
	snprintf(subtopic, sizeof(subtopic), "tele/5213d6/SENSOR");
	snprintf(value, 64, "{");

	snprintf(v, 64, JSON_KEY(BH1750) ":{\"Illuminance\":%d}" COMMA, sensor->bh1750_raw);
	strncat(value, v, 64);

	snprintf(v, 64, JSON_KEY(BMP085) ":{\"Temperature\":%.1f, \"Pressure\":%.1f}", sensor->bmp085_temp, sensor->bmp085_baro);
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

	snprintf(cvalue, 6, "%u", sensor->bh1750_raw);
	publish_sensor(BH1750, "lum_raw", cvalue);

	snprintf(cvalue, 6, "%u", sensor->bh1750_raw2);
	publish_sensor(BH1750, "lum_raw2", cvalue);

	snprintf(cvalue, 6, "%u", sensor->bh1750_lux);
	publish_sensor(BH1750, "lum_lux", cvalue);

	snprintf(cvalue, 5, "%0.1f", sensor->bmp085_temp);
	publish_sensor(BMP085, "temp", cvalue);

	snprintf(cvalue, 8, "%0.1f", sensor->bmp085_baro);
	publish_sensor(BMP085, "baro", cvalue);

	snprintf(cvalue, 5, "%0.1f", sensor->bmp280_temp);
	publish_sensor(BMP280, "temp", cvalue);

	snprintf(cvalue, 8, "%0.1f", sensor->bmp280_baro);
	publish_sensor(BMP280, "baro", cvalue);
}

static void write_sensors_sysfslike() {
	char cvalue[8];

	snprintf(cvalue, 6, "%u", sensor->bh1750_raw);
	create_sysfslike(RAM, "lum_raw", cvalue, "%s", BH1750);

	snprintf(cvalue, 6, "%u", sensor->bh1750_raw2);
	create_sysfslike(RAM, "lum_raw2", cvalue, "%s", BH1750);

	snprintf(cvalue, 6, "%u", sensor->bh1750_lux);
	create_sysfslike(RAM, "lum_lux", cvalue, "%s", BH1750);

	snprintf(cvalue, 5, "%0.1f", sensor->bmp085_temp);
	create_sysfslike(RAM, "temp", cvalue, "%s", BMP085);

	snprintf(cvalue, 8, "%0.1f", sensor->bmp085_baro);
	create_sysfslike(RAM, "baro", cvalue, "%s", BMP085);

	snprintf(cvalue, 5, "%0.1f", sensor->bmp280_temp);
	create_sysfslike(RAM, "temp", cvalue, "%s", BMP280);

	snprintf(cvalue, 8, "%0.1f", sensor->bmp280_baro);
	create_sysfslike(RAM, "baro", cvalue, "%s", BMP280);
}

static void write_sensors_json() {
	FILE *fp = fopen(RUN SLASH SENSORS_JSON, "wt");
	if (fp == NULL)
		return;

	fprintf(fp, "\{");
	fprintf(fp, JSON_INT("lumi") COMMA, sensor->lumi);
	fprintf(fp, JSON_INT("tvoc") COMMA, sensor->tvoc);
	fprintf(fp, JSON_FLOAT("tin") COMMA, sensor->tin);
	fprintf(fp, JSON_FLOAT("tout") COMMA, sensor->tout);
	fprintf(fp, JSON_FLOAT("humi") COMMA, sensor->humi);
	fprintf(fp, JSON_FLOAT(BMP085 "_TEMP") COMMA, sensor->bmp085_temp);
	fprintf(fp, JSON_FLOAT(BMP085 "_BARO") COMMA, sensor->bmp085_baro);
	fprintf(fp, JSON_FLOAT(BMP280 "_TEMP") COMMA, sensor->bmp280_temp);
	fprintf(fp, JSON_FLOAT(BMP280 "_BARO") COMMA, sensor->bmp280_baro);
	fprintf(fp, JSON_INT(BH1750 "_LUX"), sensor->bh1750_lux);
	fprintf(fp, "}");
	fflush(fp);
	fclose(fp);
}

static void loop() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	// wait for tasmota discovery
	sleep(1);

	while (1) {
		LOCALTIME

		read_bmp085();

		read_bh1750();
		mean_bh1750();

		publish_sensors_tasmotalike();
		publish_sensors();

		// update abstract sensors
		sensor->tin = TEMP_IN;
		sensor->tout = TEMP_OUT;
		sensor->humi = HUMI;
		sensor->lumi = LUMI;
		sensor->tvoc = TVOC;

		// store sensors four times per day
		if (now->tm_min == 0 || now->tm_min == 1)
			switch (now->tm_hour) {
			case 0:
				memcpy(sensor0, sensor, sizeof(sensors_t));
				break;
			case 6:
				memcpy(sensor6, sensor, sizeof(sensors_t));
				xlog("sensors6 tin=%.1f tout=%.1f  humi=%.1f lumi=%d tvoc=%d", sensor6->tin, sensor6->tout, sensor6->humi, sensor6->lumi, sensor6->tvoc);
				break;
			case 12:
				memcpy(sensor12, sensor, sizeof(sensors_t));
				break;
			case 18:
				memcpy(sensor18, sensor, sizeof(sensors_t));
				break;
			default:
			}

		if (WRITE_JSON)
			write_sensors_json();

		if (WRITE_SYSFSLIKE)
			write_sensors_sysfslike();

		// store state once per day
		if (DAILY)
			store_blob(STATE SLASH SENSORS_FILE, sensors, sizeof(sensors));

		sleep(60);
	}
}

static int init() {
	load_blob(STATE SLASH SENSORS_FILE, sensors, sizeof(sensors));

#if defined(PICAM) || defined(SENSORS_MAIN)
	i2cfd = open(I2C, O_RDWR);
	if (i2cfd < 0)
		return xerr("I2C BUS error");
	xlog("opened I2C device %s as %d", I2C, i2cfd);
#endif

	// clear average value buffer
	ZERO(bh1750_lux_mean);
	mean = 0;

	// initialize sensor data
	sensor->tin = UINT16_MAX;
	sensor->tout = UINT16_MAX;
	sensor->lumi = UINT16_MAX;
	sensor->humi = UINT16_MAX;

	sensor->bh1750_lux = UINT16_MAX;
	sensor->bmp085_temp = UINT16_MAX;
	sensor->bmp085_baro = UINT16_MAX;
	sensor->bmp280_temp = UINT16_MAX;
	sensor->bmp280_baro = UINT16_MAX;

	return 0;
}

static void stop() {
	store_blob(STATE SLASH SENSORS_FILE, sensors, sizeof(sensors));

	if (i2cfd > 0)
		close(i2cfd);
}

int sensor_main(int argc, char **argv) {
	mcp_init();
	mcp_loop();
	sleep(1);

	while (1) {
		xlog(BH1750" raw  %d", sensor->bh1750_raw);
		xlog(BH1750" raw2 %d", sensor->bh1750_raw2);
		xlog(BH1750" lux  %d lx", sensor->bh1750_lux);

		xlog(BMP085" temp %d (raw)", sensor->bmp085_temp_raw);
		xlog(BMP085" baro %d (raw)", sensor->bmp085_baro_raw);

		xlog(BMP085" temp %0.1f °C", sensor->bmp085_temp);
		xlog(BMP085" baro %0.1f hPa", sensor->bmp085_baro);

		xlog(BMP280" temp %0.1f °C", sensor->bmp280_temp);
		xlog(BMP280" baro %0.1f hPa", sensor->bmp280_baro);

		sleep(10);
	}

	mcp_stop();
	return 0;
}

#ifdef SENSORS_MAIN
int main(int argc, char **argv) {
	return sensor_main(argc, argv);
}
#endif

MCP_REGISTER(sensors, 5, &init, &stop, &loop);
