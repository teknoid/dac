#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "mcp.h"
#include "mqtt.h"
#include "xmas.h"
#include "utils.h"
#include "frozen.h"
#include "fronius.h"
#include "flamingo.h"
#include "tasmota-config.h"

#define MEAN	10

static tasmota_state_t *tasmota_state = NULL;

static unsigned int bh1750_lux_mean[MEAN];
static int mean;

//static void dump(const char *prefix, unsigned int id, const char *topic, uint16_t tsize, const char *message, size_t msize) {
//	char *t = make_string(topic, tsize);
//	char *m = make_string(message, msize);
//	xlog("%s %06X topic('%s') = %s", prefix, id, t, m);
//	free(t);
//	free(m);
//}

static void write_sensors_json() {
	FILE *fp = fopen(SENSORS_JSON, "w");
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

// topic('tele/7ECDD0/SENSOR') = {"Time":"2024-05-24T10:23:02","Switch1":"OFF"}
//             ^^^^^^
static unsigned int get_id(const char *topic, size_t size) {
	int slash1 = 0, slash2 = 0;

	for (int i = 0; i < size; i++)
		if (topic[i] == '/') {
			if (!slash1)
				slash1 = i;
			else if (!slash2)
				slash2 = i;
		}

	if (slash1 && slash2 && ((slash2 - slash1) == 7)) {
		char id[7];
		memcpy(id, &topic[slash1 + 1], 6);
		id[6] = '\0';
		long l = strtol(id, NULL, 16);
		return (unsigned int) l;
	}

	return 0;
}

static const tasmota_config_t* get_config(unsigned int id) {
	for (int i = 0; i < ARRAY_SIZE(tasmota_config); i++)
		if (tasmota_config[i].id == id)
			return &tasmota_config[i];

	return 0;
}

// find existing tasmota state or create a new one
static tasmota_state_t* get_state(unsigned int id) {
	tasmota_state_t *ts = tasmota_state;
	while (ts != NULL) {
		if (ts->id == id)
			return ts;
		ts = ts->next;
	}

	tasmota_state_t *ts_new = malloc(sizeof(tasmota_state_t));
	ts_new->id = id;
	ts_new->relay1 = -1;
	ts_new->relay2 = -1;
	ts_new->relay3 = -1;
	ts_new->relay4 = -1;
	ts_new->position = -1;
	ts_new->timer = 0;
	ts_new->next = NULL;

	if (tasmota_state == NULL)
		// this is the head
		tasmota_state = ts_new;
	else {
		// append to last in chain
		ts = tasmota_state;
		while (ts->next != NULL)
			ts = ts->next;
		ts->next = ts_new;
	}

	xlog("TASMOTA created new state for %06X", ts_new->id);
	return ts_new;
}

// execute tasmota BACKLOG command via mqtt publish
static int backlog(unsigned int id, const char *cmd) {
	char topic[32];
	snprintf(topic, 32, "cmnd/%6X/Backlog", id);
	xlog("TASMOTA executing backlog command %6X :: %s", topic, cmd);
	return publish(topic, cmd);
}

// update tasmota relay power state
static void update_relay(unsigned int id, int relay, int power) {
	tasmota_state_t *ss = get_state(id);
	if (relay == 0 || relay == 1) {
		ss->relay1 = power;
		xlog("TASMOTA updated %6X relay1 state to %d", ss->id, power);
	} else if (relay == 2) {
		ss->relay2 = power;
		xlog("TASMOTA updated %6X relay2 state to %d", ss->id, power);
	} else if (relay == 3) {
		ss->relay3 = power;
		xlog("TASMOTA updated %6X relay3 state to %d", ss->id, power);
	} else if (relay == 4) {
		ss->relay4 = power;
		xlog("TASMOTA updated %6X relay4 state to %d", ss->id, power);
	} else
		xlog("TASMOTA no relay%d at %6X", ss->id, relay);
}

// update tasmota shutter position
static void update_shutter(unsigned int id, unsigned int position) {
	tasmota_state_t *ss = get_state(id);
	ss->position = position;
	xlog("TASMOTA updated %6X shutter position to %d", ss->id, position);
}

// trigger a button press event
static void trigger(unsigned int id, int button, int action) {

	// we do not track button 'release', only button 'press'
	if (!action)
		return;

	xlog("TASMOTA trigger %6X %d %d", id, button, action);

	if (id == KUECHE && button == 2) {
		// forcing first boiler to heat up for 10 minutes
#ifdef FRONIUS
		fronius_override("boiler1");
#endif
		return;
	}

	// check tasmota-config.h
	for (int i = 0; i < ARRAY_SIZE(tasmota_config); i++) {
		tasmota_config_t sc = tasmota_config[i];
		tasmota_state_t *ss = get_state(sc.id);
		int power;
		if (sc.relay == 0 || sc.relay == 1)
			power = ss->relay1;
		else if (sc.relay == 2)
			power = ss->relay2;
		else if (sc.relay == 3)
			power = ss->relay3;
		else if (sc.relay == 4)
			power = ss->relay4;

		if (sc.t1 == id && sc.t1b == button) {
			if (power != 1)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}

		if (sc.t2 == id && sc.t2b == button) {
			if (power != 1)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}

		if (sc.t3 == id && sc.t3b == button) {
			if (power != 1)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}

		if (sc.t4 == id && sc.t4b == button) {
			if (power != 1)
				tasmota_power(sc.id, sc.relay, 1);
			else
				tasmota_power(sc.id, sc.relay, 0);
		}
	}
}

// decode flamingo message
static int flamingo(unsigned int code) {
	uint16_t xmitter;
	uint8_t command, channel, payload, rolling;

	xlog("TASMOTA flamingo");
	flamingo28_decode(code, &xmitter, &command, &channel, &payload, &rolling);

	switch (xmitter) {
	case 0x835a:
		switch (channel) {
		case 1:
			if (command == 2)
				xmas_on();
			else if (command == 0)
				xmas_off();
			break;
		default:
		}
		break;
	default:
	}

	return 0;
}

static void bh1750_calc_mean() {
	bh1750_lux_mean[mean++] = sensors->bh1750_lux;
	if (mean == MEAN)
		mean = 0;

	unsigned long sum = 0;
	for (int i = 0; i < MEAN; i++)
		sum += bh1750_lux_mean[i];

	sensors->bh1750_lux_mean = sum / MEAN;
}

static void dispatch_button(unsigned int id, const char *topic, uint16_t tsize, const char *message, size_t msize) {
	char fmt[32], a[5];

	for (int i = 0; i < 8; i++) {
		snprintf(fmt, 32, "{Switch%d:%%Q}", i);
		char *sw = NULL;

		if (json_scanf(message, msize, fmt, &sw)) {

			// Shelly1+2
			if (!strcmp(sw, ON))
				trigger(id, i, 1);
			else if (!strcmp(sw, OFF))
				trigger(id, i, 0);
			else {

				// Shelly4
				if (json_scanf(sw, strlen(sw), "{Action:%s}", &a)) {
					if (!strcmp(a, ON))
						trigger(id, i, 1);
					else
						trigger(id, i, 0);
				}
			}
			free(sw);
		}
	}
}

static int dispatch_tele_sensor(unsigned int id, const char *topic, uint16_t tsize, const char *message, size_t msize) {
	char *bh1750 = NULL;
	char *bmp280 = NULL;
	char *bmp085 = NULL;
	char *sht31 = NULL;
	char *htu21 = NULL;
	char *analog = NULL;

	json_scanf(message, msize, "{BH1750:%Q, BMP280:%Q, BMP085:%Q, SHT3X:%Q, HTU21:%Q, ANALOG:%Q}", &bh1750, &bmp280, &bmp085, &sht31, &htu21, &analog);

	if (bh1750 != NULL) {
		json_scanf(bh1750, strlen(bh1750), "{Illuminance:%d}", &sensors->bh1750_lux);
		bh1750_calc_mean();
		// xdebug("TASMOTA BH1750 %d lux, %d lux mean", sensors->bh1750_lux, sensors->bh1750_lux_mean);
		free(bh1750);
	}

	if (bmp280 != NULL) {
		json_scanf(bmp280, strlen(bmp280), "{Temperature:%f, Pressure:%f}", &sensors->bmp280_temp, &sensors->bmp280_baro);
		// xdebug("TASMOTA BMP280 %.1f °C, %.1f hPa", sensors->bmp280_temp, sensors->bmp280_baro);
		free(bmp280);
	}

	if (bmp085 != NULL) {
		json_scanf(bmp085, strlen(bmp085), "{Temperature:%f, Pressure:%f}", &sensors->bmp085_temp, &sensors->bmp085_baro);
		// xdebug("TASMOTA BMP085 %.1f °C, %.1f hPa", sensors->bmp085_temp, sensors->bmp085_baro);
		free(bmp085);
	}

	if (sht31 != NULL) {
		json_scanf(sht31, strlen(sht31), "{Temperature:%f, Humidity:%f, DewPoint:%f}", &sensors->sht31_temp, &sensors->sht31_humi, &sensors->sht31_dew);
		// xdebug("TASMOTA SHT31 %.1f °C, humidity %.1f %, dewpoint %.1f °C", sensors->sht31_temp, sensors->sht31_humi, sensors->sht31_dew);
		free(sht31);
	}

	if (htu21 != NULL) {
		json_scanf(htu21, strlen(htu21), "{Temperature:%f, Humidity:%f, DewPoint:%f}", &sensors->htu21_temp, &sensors->htu21_humi, &sensors->htu21_dew);
		// xdebug("TASMOTA HTU21 %.1f °C, humidity %.1f %, dewpoint %.1f °C", sensors->htu21_temp, sensors->htu21_humi, sensors->htu21_dew);
		free(sht31);
	}

	if (analog != NULL) {
		json_scanf(analog, strlen(analog), "{A0:%d}", &sensors->ml8511_uv);
		// xdebug("TASMOTA ML8511 %d mV", sensors->ml8511_uv);
		free(analog);
	}

	// TASMOTA 2FEFEE topic('tele/2FEFEE/SENSOR') = {"Time":"2024-05-24T14:09:31","Switch1":"OFF","Switch2":"ON","ANALOG":{"Temperature":40.3},"TempUnit":"C"}
	dispatch_button(id, topic, tsize, message, msize);

	return 0;
}

static int dispatch_tele_result(unsigned int id, const char *topic, uint16_t tsize, const char *message, size_t msize) {
	char *rf = NULL;

	json_scanf(message, msize, "{RfReceived:%Q}", &rf);

	if (rf != NULL) {
		unsigned int data, bits, proto, pulse;

		json_scanf(rf, strlen(rf), "{Data:%x, Bits:%d, Protocol:%d, Pulse:%d}", &data, &bits, &proto, &pulse);
		free(rf);

		if (bits < 16 || (data & 0xffff) == 0xffff) {
			// xlog("TASMOTA RF noise data=0x%x bits=%d protocol=%d pulse=%d", data, bits, proto, pulse);
			return 0;
		}

		if (data == DOORBELL)
			return notify("Ding", "Dong", "ding-dong.wav");

		if (bits == 28)
			return flamingo(data);

		xlog("TASMOTA unknown RF received data=0x%x bits=%d protocol=%d pulse=%d", data, bits, proto, pulse);
	}

	return 0;
}

static int dispatch_tele(unsigned int id, const char *topic, uint16_t tsize, const char *message, size_t msize) {
	if (ends_with("SENSOR", topic, tsize))
		return dispatch_tele_sensor(id, topic, tsize, message, msize);

	if (ends_with("RESULT", topic, tsize))
		return dispatch_tele_result(id, topic, tsize, message, msize);

	return 0;
}

static int dispatch_cmnd(unsigned int id, const char *topic, uint16_t tsize, const char *message, size_t msize) {
	return 0;
}

static int dispatch_stat(unsigned int id, const char *topic, uint16_t tsize, const char *message, size_t msize) {
	char a[5];
	int i;

	// scan for relay power state results
	if (json_scanf(message, msize, "{POWER:%s}", &a))
		update_relay(id, 0, !strcmp(a, ON) ? 1 : 0);
	if (json_scanf(message, msize, "{POWER1:%s}", &a))
		update_relay(id, 1, !strcmp(a, ON) ? 1 : 0);
	if (json_scanf(message, msize, "{POWER2:%s}", &a))
		update_relay(id, 2, !strcmp(a, ON) ? 1 : 0);

	// scan for shutter position results
	char *sh = NULL;
	if (json_scanf(message, msize, "{Shutter1:%Q}", &sh)) {
		if (json_scanf(sh, strlen(sh), "{Position:%d}", &i))
			update_shutter(id, i);
		free(sh);
	}

	// TASMOTA B20670 topic('stat/B20670/RESULT') = {"Switch3":{"Action":"ON"}}
	dispatch_button(id, topic, tsize, message, msize);

	return 0;
}

// handle a subscribed mqtt message
int tasmota_dispatch(const char *topic, uint16_t tsize, const char *message, size_t msize) {
	unsigned int id = get_id(topic, tsize);
	if (!id)
		return 0;

	// dump("TASMOTA", id, topic, tsize, message, msize);

	// TELE
	if (starts_with(TOPIC_TELE, topic, tsize))
		return dispatch_tele(id, topic, tsize, message, msize);

	// CMND
	if (starts_with(TOPIC_CMND, topic, tsize))
		return dispatch_cmnd(id, topic, tsize, message, msize);

	// STAT
	if (starts_with(TOPIC_STAT, topic, tsize))
		return dispatch_stat(id, topic, tsize, message, msize);

	return 0;
}

// execute tasmota POWER command to get device state
int tasmota_power_get(unsigned int id, int relay) {
	if (!id)
		return 0;

	char topic[32];
	if (relay)
		snprintf(topic, 32, "cmnd/%6X/POWER%d", id, relay);
	else
		snprintf(topic, 32, "cmnd/%6X/POWER", id);
	xlog("TASMOTA requesting power state from %6X:%d", id, relay);

	// publish and wait for updating state
	publish(topic, 0);
	msleep(500);

	tasmota_state_t *ss = get_state(id);
	switch (relay) {
	case 0:
	case 1:
		return ss->relay1;
	case 2:
		return ss->relay2;
	case 3:
		return ss->relay3;
	case 4:
		return ss->relay4;
	default:
		return -1;
	}
}

// execute tasmota POWER ON/OFF command via mqtt publish
int tasmota_power(unsigned int id, int relay, int power) {
	if (!id)
		return 0;

	char topic[32];
	if (relay)
		snprintf(topic, 32, "cmnd/%6X/POWER%d", id, relay);
	else
		snprintf(topic, 32, "cmnd/%6X/POWER", id);

	tasmota_state_t *ss = get_state(id);
	if (power) {
		// start timer if configured
		const tasmota_config_t *sc = get_config(id);
		if (sc && sc->timer) {
			ss->timer = sc->timer;
			xlog("TASMOTA started timer for %06X %d", ss->id, ss->timer);
		}
		xlog("TASMOTA switching %6X:%d %s", id, relay, ON);
		return publish(topic, ON);
	} else {
		xlog("TASMOTA switching %6X:%d %s", id, relay, OFF);
		return publish(topic, OFF);
	}
}

int tasmota_power_on(unsigned int id) {
	return tasmota_power(id, 0, 1);
}

int tasmota_power_off(unsigned int id) {
	return tasmota_power(id, 0, 0);
}

// execute tasmota shutter up/down
int tasmota_shutter(unsigned int id, unsigned int target) {
	if (target == SHUTTER_POS)
		return backlog(id, "ShutterPosition");

	tasmota_state_t *ss = get_state(id);
	if (target == SHUTTER_DOWN && ss->position != SHUTTER_DOWN)
		return backlog(id, "ShutterClose");

	if (target == SHUTTER_UP && ss->position != SHUTTER_UP)
		return backlog(id, "ShutterOpen");

	char value[20];
	snprintf(value, 20, "ShutterPosition %d", target);
	return backlog(id, value);
}

static void loop() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	sleep(1);
	write_sensors_json();

	while (1) {
		sleep(1);

		// decrease timers and switch off if timer reached 0
		tasmota_state_t *ts = tasmota_state;
		while (ts != NULL) {
			if (ts->timer) {
				ts->timer--;
				if (ts->timer == 0) {
					if (ts->relay1)
						tasmota_power(ts->id, 1, 0);
					if (ts->relay2)
						tasmota_power(ts->id, 2, 0);
					if (ts->relay3)
						tasmota_power(ts->id, 3, 0);
					if (ts->relay4)
						tasmota_power(ts->id, 4, 0);
				}
			}
			ts = ts->next;
		}

		time_t now_ts = time(NULL);
		if (now_ts % 60 == 0)
			write_sensors_json();
	}
}

static int init() {
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
}

MCP_REGISTER(tasmota, 3, &init, &stop, &loop);
