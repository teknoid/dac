#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>

#include "sensors.h"
#include "utils.h"
#include "i2c.h"
#include "mcp.h"

#define SYSFSLIKE	0

static pthread_t thread;
static int i2cfd;

#ifdef SENSORS_MAIN
mcp_sensors_t *sensors = NULL;
#endif

// bisher gefühlt bei 100 Lux (19:55)
// Straßenlampe Eisenstraße 0x40 = XX Lux
// Straßenlampe Wiesenstraße bei 0x08 = 6 Lux (20:15)
// Kamera aus bei 0x02 = 1 Lux (20:22)
// 0x00 (20:25) aber noch deutlich hell am Westhorizont

// https://forums.raspberrypi.com/viewtopic.php?t=38023
static void read_bh1750() {
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
}

// https://forums.raspberrypi.com/viewtopic.php?t=16968
static void read_bmp085() {

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

static void write_sysfslike() {
	char cvalue[8];

	snprintf(cvalue, 6, "%u", sensors->bh1750_raw);
	create_sysfslike(DIRECTORY, "lum_raw", cvalue, "%s", BH1750);

	snprintf(cvalue, 6, "%u", sensors->bh1750_raw2);
	create_sysfslike(DIRECTORY, "lum_raw2", cvalue, "%s", BH1750);

	snprintf(cvalue, 6, "%u", sensors->bh1750_lux);
	create_sysfslike(DIRECTORY, "lum_lux", cvalue, "%s", BH1750);

	snprintf(cvalue, 4, "%u", sensors->bh1750_prc);
	create_sysfslike(DIRECTORY, "lum_percent", cvalue, "%s", BH1750);

	snprintf(cvalue, 5, "%0.1f", sensors->bmp085_temp);
	create_sysfslike(DIRECTORY, "temp", cvalue, "%s", BMP085);

	snprintf(cvalue, 8, "%0.1f", sensors->bmp085_baro);
	create_sysfslike(DIRECTORY, "baro", cvalue, "%s", BMP085);
}

static void* loop(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	while (1) {
		read_bh1750();
		read_bmp085();

		if (SYSFSLIKE)
			write_sysfslike();

		sleep(60);
	}
}

static int init() {
	// TODO config
	i2cfd = open(I2CBUS, O_RDWR);
	if (i2cfd < 0)
		xlog("I2C BUS error");

	if (pthread_create(&thread, NULL, &loop, NULL))
		xlog("Error creating sensors thread");

	return 0;
}

static void stop() {
	if (thread) {
		if (pthread_cancel(thread))
			xlog("Error canceling sensors thread");
		if (pthread_join(thread, NULL))
			xlog("Error joining sensors thread");
	}

	if (i2cfd > 0)
		close(i2cfd);
}

int sensor_main(int argc, char **argv) {
	sensors = malloc(sizeof(*sensors));
	ZERO(sensors);

	init();
	sleep(1);

	while (1) {
		printf("BH1750 raw  %d\n", sensors->bh1750_raw);
		printf("BH1750 raw2 %d\n", sensors->bh1750_raw2);
		printf("BH1750 lux  %d lx\n", sensors->bh1750_lux);
		printf("BH1750 prc  %d %%\n", sensors->bh1750_prc);

		printf("BMP085 temp %d (raw)\n", sensors->bmp085_temp_raw);
		printf("BMP085 baro %d (raw)\n", sensors->bmp085_baro_raw);

		printf("BMP085 temp %0.1f °C\n", sensors->bmp085_temp);
		printf("BMP085 baro %0.1f hPa\n", sensors->bmp085_baro);

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
MCP_REGISTER(sensors, 3, &init, &stop);
#endif
