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
#include "mqtt.h"
#include "i2c.h"
#include "mcp.h"

#define SWAP(X) ((X<<8) & 0xFF00) | ((X>>8) & 0xFF)

static const char *topic = "sensor";

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
}

// read BMP085 calibration data
static void init_bmp085() {
}

static void publish_sensor(const char *sensor, const char *name, const char *value) {
	char subtopic[64];
	snprintf(subtopic, sizeof(subtopic), "%s/%s/%s", topic, sensor, name);

#ifndef SENSORS_MAIN
	publish(subtopic, value);
#endif
}

static void publish_mqtt() {
	char cvalue[8];

	snprintf(cvalue, 6, "%u", sensors->bh1750_raw);
	publish_sensor(BH1750, "lum_raw", cvalue);

	snprintf(cvalue, 6, "%u", sensors->bh1750_raw2);
	publish_sensor(BH1750, "lum_raw2", cvalue);

	snprintf(cvalue, 6, "%u", sensors->bh1750_lux);
	publish_sensor(BH1750, "lum_lux", cvalue);

	snprintf(cvalue, 4, "%u", sensors->bh1750_prc);
	publish_sensor(BH1750, "lum_percent", cvalue);

	snprintf(cvalue, 5, "%2.1f", sensors->bmp085_temp);
	publish_sensor(BMP085, "temp", cvalue);

	snprintf(cvalue, 8, "%4.1f", sensors->bmp085_baro);
	publish_sensor(BMP085, "baro", cvalue);
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

	snprintf(cvalue, 5, "%2.1f", sensors->bmp085_temp);
	create_sysfslike(DIRECTORY, "temp", cvalue, "%s", BMP085);

	snprintf(cvalue, 8, "%4.1f", sensors->bmp085_baro);
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
		// write_sysfslike();
		publish_mqtt();
		sleep(60);
	}
}

static int init() {
	// TODO config
	i2cfd = open(I2CBUS, O_RDWR);
	if (i2cfd < 0)
		xlog("I2C BUS error");

	init_bmp085();

#ifndef SENSORS_MAIN
	if (pthread_create(&thread, NULL, &loop, NULL))
		xlog("Error creating sensors thread");
#endif

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

#ifdef SENSORS_MAIN
int main(int argc, char **argv) {
	sensors = malloc(sizeof(*sensors));
	ZERO(sensors);

	init();

	read_bh1750();
	read_bmp085();

	printf("BH1750 raw  %d\n", sensors->bh1750_raw);
	printf("BH1750 raw2 %d\n", sensors->bh1750_raw2);
	printf("BH1750 lux  %d lx\n", sensors->bh1750_lux);
	printf("BH1750 prc  %d %%\n", sensors->bh1750_prc);

	printf("BMP085 temp %d (raw)\n", sensors->bmp085_utemp);
	printf("BMP085 baro %d (raw)\n", sensors->bmp085_ubaro);

	printf("BMP085 temp %0.1f °C\n", sensors->bmp085_temp);
	printf("BMP085 baro %0.1f hPa\n", sensors->bmp085_baro);

	stop();
	return 0;
}
#endif

#ifndef SENSORS_MAIN
MCP_REGISTER(sensors, 3, &init, &stop);
#endif
