#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

#include "tasmota.h"
#include "tasmota-config.h"
#include "flamingo.h"
#include "frozen.h"
#include "solar.h"
#include "utils.h"
#include "xmas.h"
#include "mqtt.h"
#include "mcp.h"

#define MESSAGE_ON			(message[0] == 'O' && message[1] == 'N')
#define DISCOVERY			"discovery"
#define ON					"ON"
#define OFF					"OFF"
#define ONLINE				"Online"
#define OFFLINE				"Offline"

#define TOPIC_LEN			32
#define PREFIX_LEN			32
#define SUFFIX_LEN			32
#define BUF32				32

static tasmota_t *tasmota = NULL;

// topic('tele/7ECDD0/SENSOR1') = {"Time":"2024-05-24T10:23:02","Switch1":"OFF"}
//        ^    ^      ^     ^
//        |    |      |     |
//        |    |      |     -- index
//        |    |      -------- suffix
//        |    --------------- id
//        -------------------- prefix
static void split(const char *topic, size_t size, unsigned int *id, char *prefix, char *suffix, unsigned int *idx) {
	int slash1 = 0, slash2 = 0;

	// check size
	if (size > (PREFIX_LEN + SUFFIX_LEN + 6 + 2)) {
		xlog("TASMOTA topic size exceeds buffer");
		return;
	}

	// find the two slashes
	for (int i = 0; i < size; i++)
		if (topic[i] == '/') {
			if (!slash1)
				slash1 = i;
			else if (!slash2)
				slash2 = i;
		}

	// prefix
	if (slash1) {
		memcpy(prefix, topic, slash1);
		prefix[slash1] = '\0';
	}

	// id
	if (slash1 && slash2 && ((slash2 - slash1) == 7)) {
		char idc[7];
		memcpy(idc, &topic[slash1 + 1], 6);
		idc[6] = '\0';
		*id = (unsigned int) strtol(idc, NULL, 16);
	}

	// suffix
	if (slash2) {
		int len = size - slash2 - 1;
		memcpy(suffix, &topic[slash2 + 1], len);
		suffix[len] = '\0';

		// index
		*idx = 0;
		if (suffix[len - 1] >= '0' && suffix[len - 1] <= '9') {
			*idx = suffix[len - 1] - '0';
			suffix[len - 1] = '\0';
		}
		if (suffix[len - 2] >= '0' && suffix[len - 2] <= '9')
			suffix[len - 2] = '\0';
	}
}

static const tasmota_config_t* get_config(unsigned int id) {
	for (int i = 0; i < ARRAY_SIZE(tasmota_config); i++)
		if (tasmota_config[i].id == id)
			return &tasmota_config[i];

	return 0;
}

// find existing tasmota state or create a new one
static tasmota_t* get_by_id(unsigned int id) {
	tasmota_t *t = tasmota;
	while (t != NULL) {
		if (t->id == id)
			return t;
		t = t->next;
	}

	tasmota_t *tnew = malloc(sizeof(tasmota_t));
	tnew->id = id;
	tnew->online = 0;
	tnew->next = NULL;

	if (tasmota == NULL)
		// this is the head
		tasmota = tnew;
	else {
		// append to last in chain
		t = tasmota;
		while (t->next != NULL)
			t = t->next;
		t->next = tnew;
	}

	xdebug("TASMOTA %06X created new state", tnew->id);
	return tnew;
}

// execute tasmota BACKLOG command via mqtt publish
static int backlog(unsigned int id, const char *message) {
	char topic[TOPIC_LEN];
	snprintf(topic, TOPIC_LEN, "cmnd/%6X/Backlog", id);
	xlog("TASMOTA %06X executing backlog command :: %s", id, message);
	return publish(topic, message, 0);
}

// trigger a button press event
static void trigger(tasmota_t *t, int button, int action) {

	// we do not track button 'release', only button 'press'
	if (!action)
		return;

	xlog("TASMOTA %06X trigger %d %d", t->id, button, action);

	if (t->id == KUECHE && button == 2) {
		// forcing first boiler to heat up for 10 minutes
#ifdef SOLAR
		solar_toggle_name("boiler1");
#endif
		return;
	}

	// check tasmota-config.h
	for (int i = 0; i < ARRAY_SIZE(tasmota_config); i++) {
		tasmota_config_t tc = tasmota_config[i];
		int power = 0;
		if (tc.relay == 0)
			power = t->relay[0];
		else if (tc.relay == 1)
			power = t->relay[1];
		else if (tc.relay == 2)
			power = t->relay[2];
		else if (tc.relay == 3)
			power = t->relay[3];
		else if (tc.relay == 4)
			power = t->relay[4];

		if (tc.t1 == t->id && tc.t1b == button) {
			if (power != 1)
				tasmota_power(tc.id, tc.relay, 1);
			else
				tasmota_power(tc.id, tc.relay, 0);
		}

		if (tc.t2 == t->id && tc.t2b == button) {
			if (power != 1)
				tasmota_power(tc.id, tc.relay, 1);
			else
				tasmota_power(tc.id, tc.relay, 0);
		}

		if (tc.t3 == t->id && tc.t3b == button) {
			if (power != 1)
				tasmota_power(tc.id, tc.relay, 1);
			else
				tasmota_power(tc.id, tc.relay, 0);
		}

		if (tc.t4 == t->id && tc.t4b == button) {
			if (power != 1)
				tasmota_power(tc.id, tc.relay, 1);
			else
				tasmota_power(tc.id, tc.relay, 0);
		}
	}
}

// decode flamingo message
static int flamingo(unsigned int code) {
#ifdef FLAMINGO
	uint16_t xmitter;
	uint8_t command, channel, payload, rolling;

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
#endif
	return 0;
}

// update tasmota shutter position
static int update_shutter(tasmota_t *t, unsigned int position) {
	t->position = position;
	xlog("TASMOTA %06X updated shutter position to %d", t->id, position);
	return 0;
}

// update tasmota relay power state
static void update_power(tasmota_t *t, unsigned int relay, unsigned int power) {
	t->relay[relay] = power;

	// set relay0 and relay1 identical - tamota responds always with POWER1 even if only one relay is configured
	if (relay == 0)
		t->relay[1] = power;
	if (relay == 1)
		t->relay[0] = power;

	xlog("TASMOTA %06X update relay%d state to %d", t->id, relay, power);
}

static void scan_power(tasmota_t *t, const char *message, size_t msize) {
	unsigned int ip;
	char cp[8];

#define CP_ON (cp[0] == 'O' && cp[1] == 'N')

	// tele/5E40EC/STATE {"Time":"2025-12-28T01:12:48","Uptime":"28T13:59:11","UptimeSec":2469551,"POWER1":"OFF","POWER2":"OFF", ...
	if (json_scanf(message, msize, "{POWER:%s}", &cp))
		update_power(t, 0, CP_ON);
	if (json_scanf(message, msize, "{POWER1:%s}", &cp))
		update_power(t, 1, CP_ON);
	if (json_scanf(message, msize, "{POWER2:%s}", &cp))
		update_power(t, 2, CP_ON);
	if (json_scanf(message, msize, "{POWER3:%s}", &cp))
		update_power(t, 3, CP_ON);
	if (json_scanf(message, msize, "{POWER4:%s}", &cp))
		update_power(t, 4, CP_ON);

	// stat/5E40EC/STATUS {"Status":{"Module":0,"DeviceName":"plug6","FriendlyName":["plug6",""],"Topic":"5E40EC","ButtonTopic":"0","Power":"00","PowerLock":"00", ...
	if (json_scanf(message, msize, "{Power:%d}", &ip))
		update_power(t, 0, ip);
	if (json_scanf(message, msize, "{Power1:%d}", &ip))
		update_power(t, 1, ip);
	if (json_scanf(message, msize, "{Power2:%d}", &ip))
		update_power(t, 2, ip);
	if (json_scanf(message, msize, "{Power3:%d}", &ip))
		update_power(t, 3, ip);
	if (json_scanf(message, msize, "{Power4:%d}", &ip))
		update_power(t, 4, ip);
}

static void scan_sensor(tasmota_t *t, const char *message, size_t msize) {
	char buffer[BUFSIZE];
	char *analog = NULL;
	char *ds18b20 = NULL;
	char *bh1750 = NULL;
	char *bmp280 = NULL;
	char *bmp085 = NULL;
	char *sht31 = NULL;
	char *htu21 = NULL;
	char *gp8403 = NULL;

#define PATTERN_SENSORS "{ANALOG:%Q, DS18B20:%Q, BH1750:%Q, BMP280:%Q, BMP085:%Q, SHT3X:%Q, HTU21:%Q, GP8403:%Q}"
	json_scanf(message, msize, PATTERN_SENSORS, &analog, &ds18b20, &bh1750, &bmp280, &bmp085, &sht31, &htu21, &gp8403);

	if (analog != NULL) {
		json_scanf(analog, strlen(analog), "{A0:%d}", &t->ml8511_uv);
		free(analog);
		xdebug("TASMOTA sensor ML8511 %d mV", t->ml8511_uv);
	}

	if (ds18b20 != NULL) {
		json_scanf(ds18b20, strlen(ds18b20), "{Id:%s, Temperature:%f}", &buffer, &t->ds18b20_temp);
		t->ds18b20_id = (unsigned int) strtol(buffer, NULL, 16);
		free(ds18b20);
		xdebug("TASMOTA sensor DS18B20 id %d, %.1f °C", t->ds18b20_id, t->ds18b20_temp);
	}

	if (bh1750 != NULL) {
		json_scanf(bh1750, strlen(bh1750), "{Illuminance:%d}", &t->bh1750_lux);
		free(bh1750);
		xdebug("TASMOTA sensor BH1750 %d lux, %d lux mean", t->bh1750_lux, t->bh1750_lux_mean);
	}

	if (bmp280 != NULL) {
		json_scanf(bmp280, strlen(bmp280), "{Temperature:%f, Pressure:%f}", &t->bmp280_temp, &t->bmp280_baro);
		free(bmp280);
		xdebug("TASMOTA sensor BMP280 %.1f °C, %.1f hPa", t->bmp280_temp, t->bmp280_baro);
	}

	if (bmp085 != NULL) {
		json_scanf(bmp085, strlen(bmp085), "{Temperature:%f, Pressure:%f}", &t->bmp085_temp, &t->bmp085_baro);
		free(bmp085);
		xdebug("TASMOTA sensor BMP085 %.1f °C, %.1f hPa", t->bmp085_temp, t->bmp085_baro);
	}

	if (sht31 != NULL) {
		json_scanf(sht31, strlen(sht31), "{Temperature:%f, Humidity:%f, DewPoint:%f}", &t->sht31_temp, &t->sht31_humi, &t->sht31_dew);
		free(sht31);
		xdebug("TASMOTA sensor SHT31 %.1f °C, humidity %.1f %, dewpoint %.1f °C", t->sht31_temp, t->sht31_humi, t->sht31_dew);
	}

	if (htu21 != NULL) {
		json_scanf(htu21, strlen(htu21), "{Temperature:%f, Humidity:%f, DewPoint:%f}", &t->htu21_temp, &t->htu21_humi, &t->htu21_dew);
		free(sht31);
		xdebug("TASMOTA sensor HTU21 %.1f °C, humidity %.1f %, dewpoint %.1f °C", t->htu21_temp, t->htu21_humi, t->htu21_dew);
	}

	if (gp8403 != NULL) {
		json_scanf(gp8403, strlen(gp8403), "{vc0:%d, vc1:%d, pc0:%d, pc1:%d}", &t->gp8403_vc0, &t->gp8403_vc1, &t->gp8403_pc0, &t->gp8403_pc1);
		free(gp8403);
		xdebug("TASMOTA sensor GP8403 vc0=%d vc1=%d, pc0=%d pc1=%d", t->gp8403_vc0, t->gp8403_vc1, t->gp8403_pc0, t->gp8403_pc1);
	}
}

static int dispatch_status(tasmota_t *t, const char *message, size_t msize) {
	scan_power(t, message, msize);
	scan_sensor(t, message, msize);

#ifdef SOLAR
		// forward to solar dispatcher
		solar_tasmota(t);
#endif

	return 0;
}

static int dispatch_state(tasmota_t *t, const char *message, size_t msize) {
	scan_power(t, message, msize);

#ifdef SOLAR
		// forward to solar dispatcher
		solar_tasmota(t);
#endif

	return 0;
}

static int dispatch_sensor(tasmota_t *t, const char *message, size_t msize) {
	scan_sensor(t, message, msize);

#ifdef SOLAR
		// forward to solar dispatcher
		solar_tasmota(t);
#endif

	return 0;
}

static int dispatch_power(tasmota_t *t, unsigned int relay, unsigned int power) {
	update_power(t, relay, power);

#ifdef SOLAR
		// forward to solar dispatcher
		solar_tasmota(t);
#endif

	return 0;
}

static void dispatch_button(tasmota_t *t, int idx, const char *message, size_t msize) {
	char fmt[32], a[5];

	for (int i = 0; i < 8; i++) {
		snprintf(fmt, 32, "{Switch%d:%%Q}", i);
		char *sw = NULL;

		if (json_scanf(message, msize, fmt, &sw)) {

			// Shelly1+2
			if (!strcmp(ON, sw))
				trigger(t, i, 1);
			else if (!strcmp(OFF, sw))
				trigger(t, i, 0);
			else {

				// Shelly4
				if (json_scanf(sw, strlen(sw), "{Action:%s}", &a)) {
					if (!strcmp(ON, a))
						trigger(t, i, 1);
					else
						trigger(t, i, 0);
				}
			}
			free(sw);
		}
	}
}

static int dispatch_result(tasmota_t *t, const char *message, size_t msize) {
	char *rf = NULL;

	json_scanf(message, msize, "{RfReceived:%Q}", &rf);
	if (rf != NULL) {
		unsigned int data, bits, proto, pulse;

		json_scanf(rf, strlen(rf), "{Data:%x, Bits:%d, Protocol:%d, Pulse:%d}", &data, &bits, &proto, &pulse);
		free(rf);

		if (bits < 16 || (data & 0xffff) == 0xffff) {
			xdebug("TASMOTA RF noise data=0x%x bits=%d protocol=%d pulse=%d", data, bits, proto, pulse);
			return 0;
		}

		if (data == DOORBELL)
			return notify_red("Ding", "Dong", "ding-dong.wav");

		if (bits == 28)
			return flamingo(data);

		xlog("TASMOTA unknown RF received data=0x%x bits=%d protocol=%d pulse=%d", data, bits, proto, pulse);
	}

	return 0;
}

static int dispatch_lwt(tasmota_t *t, const char *message, size_t msize) {
	if (!strcmp(ONLINE, message))
		t->online = 1;

	if (!strcmp(OFFLINE, message))
		t->online = 0;

	xlog("TASMOTA %06X name=%s lwt=%s", t->id, t->name, t->online ? ONLINE : OFFLINE);
	return 0;
}

static int dispatch_tele(tasmota_t *t, const char *suffix, int idx, const char *message, size_t msize) {
	if (!strcmp("STATE", suffix))
		return dispatch_state(t, message, msize);

	if (!strcmp("SENSOR", suffix))
		return dispatch_sensor(t, message, msize);

	if (!strcmp("RESULT", suffix))
		return dispatch_result(t, message, msize);

	if (!strcmp("LWT", suffix))
		return dispatch_lwt(t, message, msize);

	return 0;
}

static int dispatch_cmnd(tasmota_t *t, const char *suffix, int idx, const char *message, size_t msize) {
	return 0;
}

static int dispatch_stat(tasmota_t *t, const char *suffix, int idx, const char *message, size_t msize) {
	if (!strcmp("STATUS", suffix))
		return dispatch_status(t, message, msize);

	// power state results
	if (!strcmp("POWER", suffix))
		return dispatch_power(t, idx, MESSAGE_ON);

	// PIR motion detection sensors - tasmota configuration:
	// SwitchMode1 1
	// SwitchTopic 0
	// Rule1 on Switch1#state=1 do publish stat/%topic%/PIR1 ON endon on Switch1#state=0 do Publish stat/%topic%/PIR1 OFF endon
	// Rule1 1
//	if (id == DEVKIT1 && !strcmp(suffix, "PIR") && idx == 1 && MESSAGE_ON)
//		return notify("motion", "devkit1", "au.wav");
	if (t->id == CARPORT && !strcmp("PIR", suffix) && idx == 1 && MESSAGE_ON)
		return notify("motion", "carport", "au.wav");

	// scan for shutter position results
	char *sh = NULL;
	int i;
	if (json_scanf(message, msize, "{Shutter1:%Q}", &sh)) {
		if (json_scanf(sh, strlen(sh), "{Position:%d}", &i))
			update_shutter(t, i);
		free(sh);
	}

	// TASMOTA B20670 topic('stat/B20670/RESULT') = {"Switch3":{"Action":"ON"}}
	dispatch_button(t, idx, message, msize);

	return 0;
}

static int dispatch_discovery(const char *topic, uint16_t tsize, const char *message, size_t msize) {
	char c1[BUF32], c2[BUF32], c3[BUF32], c4[BUF32], *name = NULL;

	sscanf(topic, "%7s/%9s/%12s/%30s", c1, c2, c3, c4);
	long int mac = strtol(c3, NULL, 16);
	unsigned int id = mac & 0xFFFFFF;
	tasmota_t *t = get_by_id(id);

	// xdebug("TASMOTA discovery c1=%s c2=%s c3=%s c4=%s", c1, c2, c3, c4);
	if (ends_with("config", topic, tsize)) {
		json_scanf(message, msize, "{hn:%Q}", &name);
		t->name = name;
		t->online = 1;
		xlog("TASMOTA discovery id=%06X name=%s", t->id, t->name);
	}
//	} else if (ends_with("sensors", topic, tsize))
//		scan_sensor(t, message, msize);

	return 0;
}

// handle a subscribed mqtt message
int tasmota_dispatch(const char *topic, uint16_t tsize, const char *message, size_t msize) {
	unsigned int id, idx;
	char prefix[PREFIX_LEN], suffix[SUFFIX_LEN];

	// raw topic + raw message
//	if (is_debug()) {
//		char *t = make_string(topic, tsize);
//		char *m = make_string(message, msize);
//		xdebug("TASMOTA %06X topic('%s') = %s", id, t, m);
//		free(t);
//		free(m);
//	}

	// discovery
	if (starts_with(TOPIC_TASMOTA SLASH DISCOVERY, topic, tsize))
		return dispatch_discovery(topic, tsize, message, msize);

	split(topic, tsize, &id, prefix, suffix, &idx);
	if (!id)
		return 0;

	// split topic + raw message
	if (is_debug()) {
		char *m = make_string(message, msize);
		xdebug("TASMOTA id=%06X prefix=%s suffix=%s index=%d message=%s", id, prefix, suffix, idx, m);
		free(m);
	}

	tasmota_t *t = get_by_id(id);
	t->online = 1;

	// TELE
	if (!strcmp(TOPIC_TELE, prefix))
		return dispatch_tele(t, suffix, idx, message, msize);

	// CMND
	if (!strcmp(TOPIC_CMND, prefix))
		return dispatch_cmnd(t, suffix, idx, message, msize);

	// STAT
	if (!strcmp(TOPIC_STAT, prefix))
		return dispatch_stat(t, suffix, idx, message, msize);

	return 0;
}

// execute openbeken POWER TOGGLE command via mqtt publish
int openbeken_power_toggle(unsigned int id) {
	if (!id)
		return 0;

	char topic[TOPIC_LEN], message[8];
	snprintf(topic, TOPIC_LEN, "cmnd/%08X/power", id);
	snprintf(message, 8, "TOGGLE");

	xlog("TASMOTA %08X power TOGGLE", id);
	return publish(topic, message, 0);
}

// execute openbeken POWER command via mqtt publish
int openbeken_power(unsigned int id, int p) {
	if (!id)
		return 0;

	char topic[TOPIC_LEN], message[8];
	snprintf(topic, TOPIC_LEN, "cmnd/%08X/power", id);
	snprintf(message, 8, "%s", p ? ON : OFF);

	xlog("TASMOTA %08X power %s", id, message);
	return publish(topic, message, 0);
}

// execute openbeken COLOR command via mqtt publish
int openbeken_color(unsigned int id, int r, int g, int b) {
	if (!id)
		return 0;

	r &= 0xff;
	g &= 0xff;
	b &= 0xff;

	char topic[TOPIC_LEN], message[12];
	snprintf(topic, TOPIC_LEN, "cmnd/%08X/color", id);
	snprintf(message, 12, "%02x%02x%02x", r, g, b);

	xlog("TASMOTA %08X color %s", id, message);
	return publish(topic, message, 0);
}

// execute openbeken DIMMER command via mqtt publish
int openbeken_dimmer(unsigned int id, int d) {
	if (!id)
		return 0;

	if (d > 100)
		d = 100;

	char topic[TOPIC_LEN], message[12];
	snprintf(topic, TOPIC_LEN, "cmnd/%08X/dimmer", id);
	snprintf(message, 12, "%d", d);

	xlog("TASMOTA %08X dimmer %s", id, message);
	return publish(topic, message, 0);
}

// execute openbeken SET command via mqtt publish
int openbeken_set(unsigned int id, int channel, int value) {
	if (!id)
		return 0;

	if (value > 100)
		value = 100;

	char topic[TOPIC_LEN], message[12];
	snprintf(topic, TOPIC_LEN, "%08X/%d/set", id, channel);
	snprintf(message, 12, "%d", value);

	xlog("TASMOTA %08X channel %d value %s", id, channel, message);
	return publish(topic, message, 0);
}

// execute tasmota STATUS command to get actual status
int tasmota_status_ask(unsigned int id, int status) {
	if (!id)
		return 0;

	char topic[TOPIC_LEN], message[4];
	snprintf(topic, TOPIC_LEN, "cmnd/%6X/STATUS", id);
	snprintf(message, 4, "%d", status);
	xlog("TASMOTA %06X asking status %d", id, status);
	return publish(topic, message, 0);
}

// execute tasmota POWER command to get actual state
int tasmota_power_ask(unsigned int id, int relay) {
	if (!id)
		return 0;

	char topic[TOPIC_LEN];
	if (relay)
		snprintf(topic, TOPIC_LEN, "cmnd/%6X/POWER%d", id, relay);
	else
		snprintf(topic, TOPIC_LEN, "cmnd/%6X/POWER", id);
	xlog("TASMOTA %06X asking power state of relay %d", id, relay);
	return publish(topic, 0, 0);
}

// execute tasmota POWER ON/OFF command via mqtt publish
int tasmota_power(unsigned int id, int relay, int power) {
	if (!id)
		return 0;

	char topic[TOPIC_LEN];
	if (relay)
		snprintf(topic, TOPIC_LEN, "cmnd/%6X/POWER%d", id, relay);
	else
		snprintf(topic, TOPIC_LEN, "cmnd/%6X/POWER", id);

	tasmota_t *t = get_by_id(id);
	if (!t->online)
		xlog("TASMOTA %06X offline? Try power anyway", t->id);
	if (power) {
		// start timer if configured
		const tasmota_config_t *sc = get_config(id);
		if (sc && sc->timer) {
			t->timer = sc->timer;
			xlog("TASMOTA %06X started timer %d", t->id, t->timer);
		}
		xlog("TASMOTA %06X switching relay %d %s", id, relay, ON);
		return publish(topic, ON, 0);
	} else {
		xlog("TASMOTA %06X switching relay %d %s", id, relay, OFF);
		return publish(topic, OFF, 0);
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

	tasmota_t *t = get_by_id(id);
	if (!t->online)
		xlog("TASMOTA %06X offline? Try shutter anyway", t->id);
	if (target == SHUTTER_DOWN && t->position != SHUTTER_DOWN)
		return backlog(id, "ShutterClose");

	if (target == SHUTTER_UP && t->position != SHUTTER_UP)
		return backlog(id, "ShutterOpen");

	char value[20];
	snprintf(value, 20, "ShutterPosition %d", target);
	return backlog(id, value);
}

tasmota_t* tasmota_get_by_id(unsigned int id) {
	tasmota_t *t = tasmota;
	while (t != NULL) {
		if (t->id == id)
			return t;
		t = t->next;
	}
	return 0;
}

tasmota_t* tasmota_get_by_name(const char *name) {
	tasmota_t *t = tasmota;
	while (t != NULL) {
		if (!strcmp(name, t->name))
			return t;
		t = t->next;
	}
	return 0;
}

static void loop() {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return;
	}

	while (1) {
		sleep(1);

		// decrease timers and switch off if timer reached 0
		tasmota_t *t = tasmota;
		while (t != NULL) {
			if (t->timer) {
				t->timer--;
				if (t->timer == 0) {
					for (int r = 0; r < RELAY_MAX; r++)
						if (t->relay[r])
							tasmota_power(t->id, r, 0);
				}
			}
			t = t->next;
		}
	}
}

static int init() {
	return 0;
}

static void stop() {
}

MCP_REGISTER(tasmota, 8, &init, &stop, &loop);
