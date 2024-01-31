#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <curl/curl.h>

#include "fronius-config.h"
#include "fronius.h"
#include "frozen.h"
#include "utils.h"
#include "mcp.h"

static pthread_t thread;

// curl response buffer
static CURL *curl;
static get_response_t res = { .buffer = NULL, .len = 0, .buflen = CHUNK_SIZE };

// program of the day - forecast will chose appropriate configuration SUNNY or CLOUDY
static device_t **potd;
static int potd_size;

// actual Fronius power flow data + calculations
static int charge, akku, grid, load, pv, surplus, extra, distortion;

// PV history values to calculate distortion
static int history[PV_HISTORY];
static int history_ptr = 0;

static int wait = WAIT_NEXT;
static int sock = 0;

int fronius_set_heater(void *ptr, int power) {
	device_t *heater = (device_t*) ptr;

	// fix power value if out of range
	if (power < 0)
		power = 0;
	if (power > 1)
		power = 1;

	// can we send a message
	if (heater->addr == NULL)
		return xerr("No address to send HTTP message");

	// check if update is necessary
	if (heater->power == power)
		return 0;

	if (!heater->active)
		power = 0;

	char command[128];
	if (power) {
		xlog("FRONIUS switching %s ON", heater->name);
		snprintf(command, 128, "curl --silent --output /dev/null http://%s/cm?cmnd=Power%%20On", heater->addr);
	} else {
		xlog("FRONIUS switching %s OFF", heater->name);
		snprintf(command, 128, "curl --silent --output /dev/null http://%s/cm?cmnd=Power%%20Off", heater->addr);
	}
	// send message to heater
	system(command);

	// update power value
	heater->power = power;
	wait = WAIT_NEXT;

	return 0;
}

int fronius_set_boiler(void *ptr, int power) {
	device_t *boiler = (device_t*) ptr;

	// fix power value if out of range
	if (power < 0)
		power = 0;
	if (power > 100)
		power = 100;

	// can we send a message
	if (boiler->addr == NULL)
		return xerr("No address to send UDP message");

	// check if update is necessary
	if (boiler->power == power)
		return 0;

	// standby: only update if smaller
	if (boiler->standby && power > boiler->maximum)
		return 0;

	// count down override and set power to 100%
	if (boiler->override) {
		boiler->override--;
		power = 100;
		xlog("FRONIUS Override active for %s remaining %d loops", boiler->name, boiler->override);
	} else {
		if (!boiler->active)
			power = 0;
		if (power == 0)
			boiler->standby = 0; // zero resets standby
	}

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock == 0)
		return xerr("Error creating socket");

	// write IP and port into sockaddr structure
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(boiler->addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	// convert 0..100% to 2..10V SSR control voltage
	char message[16];
	snprintf(message, 16, "%d:%d", boiler->phase_angle[power], 0);

	// send message to boiler
	xlog("FRONIUS send %s UDP %s", boiler->name, message);
	if (sendto(sock, message, strlen(message), 0, sa, sizeof(*sa)) < 0)
		return xerr("Sendto failed");

	// update power value
	boiler->power = power;
	wait = WAIT_NEXT;

	return 0;
}

static void set_devices(int power) {
	for (int i = 0; i < potd_size; i++) {
		device_t *d = potd[i];
		(d->set_function)(d, power);
	}
}

static void print_status() {
	char message[128];
	char value[5];

	strcpy(message, "FRONIUS devices active ");
	for (int i = 0; i < potd_size; i++)
		strcat(message, potd[i]->active ? "1" : "0");

	strcat(message, "   power ");
	for (int i = 0; i < potd_size; i++) {
		snprintf(value, 5, " %3d", potd[i]->power);
		strcat(message, value);
	}

	snprintf(value, 5, "%3d", wait);
	strcat(message, "   wait ");
	strcat(message, value);
	xlog(message);
}

static size_t callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
	size_t realsize = size * nmemb;
	get_response_t *xres = (get_response_t*) userdata;

	if (xres->buffer == NULL)
		xres->buffer = malloc(CHUNK_SIZE);

	while (xres->buflen < xres->len + realsize + 1) {
		xres->buffer = realloc(xres->buffer, xres->buflen + CHUNK_SIZE);
		xres->buflen += CHUNK_SIZE;
	}

	memcpy(&xres->buffer[xres->len], ptr, realsize);
	xres->len += realsize;
	xres->buffer[xres->len] = 0;

	return realsize;
}

static int forecast_SunD() {
	char line[32];
	int yesterday_h, yesterday_s, today_h, today_s, tomorrow_h, tomorrow_s;

	FILE *fp = popen("cat /tmp/SunD.txt", "r");
	if (fp == NULL)
		return xerr("FRONIUS no forecast data available");

	if (fgets(line, 32, fp) != NULL)
		if (sscanf(line, "%d=%d", &yesterday_h, &yesterday_s) != 2)
			return xerr("FRONIUS forecast yesterday parse error %s", line);

	if (fgets(line, 32, fp) != NULL)
		if (sscanf(line, "%d=%d", &today_h, &today_s) != 2)
			return xerr("FRONIUS forecast today parse error %s", line);

	if (fgets(line, 32, fp) != NULL)
		if (sscanf(line, "%d=%d", &tomorrow_h, &tomorrow_s) != 2)
			return xerr("FRONIUS forecast tomorrow parse error %s", line);

	pclose(fp);

	float yesterday_sun = (float) yesterday_s / 3600;
	float today_sun = (float) today_s / 3600;
	float tomorrow_sun = (float) tomorrow_s / 3600;
	xlog("FRONIUS sunshine hours forecast for yesterday %2.1f today %2.1f tomorrow %2.1f", yesterday_sun, today_sun, tomorrow_sun);
	// xlog("FRONIUS choosing program from weather forecast");

	// reset all devices
	set_devices(0);
	return today_sun;
}

static int forecast_Rad1h() {
	char line[8];
	int today, tomorrow, datomorrow;

	FILE *fp = popen("cat /tmp/Rad1h.txt", "r");
	if (fp == NULL)
		return xerr("FRONIUS no forecast data available");

	if (fgets(line, 8, fp) != NULL)
		if (sscanf(line, "%d", &today) != 1)
			return xerr("FRONIUS forecast parse error %s", line);

	if (fgets(line, 8, fp) != NULL)
		if (sscanf(line, "%d", &tomorrow) != 1)
			return xerr("FRONIUS forecast parse error %s", line);

	if (fgets(line, 8, fp) != NULL)
		if (sscanf(line, "%d", &datomorrow) != 1)
			return xerr("FRONIUS forecast parse error %s", line);

	pclose(fp);

	xlog("FRONIUS forecast() sunshine radiation for today %d tomorrow %d day after tomorrow %d", today, tomorrow, datomorrow);
	// xlog("FRONIUS choosing program from weather forecast");

	// Datum	Erwartet	Produziert	Akku
	// 31.01.	2980		6110		43
	// 01.02.	2870
	// 02.02.	2500

	// if today > 10 (Eigenverbrauch) + 10 * charge / 100 (zu ladender Akku)
	// SUNNY  Programm: heaterX (g), boiler1 (g), boiler2 (g), boiler3
	// else
	// CLOUDY Programm: boiler1 (g), boiler2, boiler3, heaterX

	if (today < 3000) {
		xlog("FRONIUS forecast() choosing CLOUDY program for today");
		potd = CONFIG_CLOUDY;
		potd_size = ARRAY_SIZE(CONFIG_CLOUDY);
	} else {
		xlog("FRONIUS forecast() choosing SUNNY program for today");
		potd = CONFIG_SUNNY;
		potd_size = ARRAY_SIZE(CONFIG_SUNNY);
	}

	// reset all devices
	set_devices(0);
	return today;
}

static int parse() {
	float p_charge, p_akku, p_grid, p_load, p_pv;
	char *c;
	int ret;

	// printf("Received data:/n%s\n", req.buffer);
	// json_scanf(req.buffer, req.len, "{ Body { Data { PowerReal_P_Sum:%f } } }", &grid_power);
	// printf("Grid Power %f\n", grid_power);

	ret = json_scanf(res.buffer, res.len, "{ Body { Data { Site { P_Akku:%f, P_Grid:%f, P_Load:%f, P_PV:%f } } } }", &p_akku, &p_grid, &p_load, &p_pv);
	if (ret != 4)
		return -1;

	// workaround parsing { "Inverters" : { "1" : { ... } } }
	ret = json_scanf(res.buffer, res.len, "{ Body { Data { Inverters:%Q } } }", &c);
	if (ret != 1)
		return -2;

	if (c != NULL) {
		char *p = c;
		while (*p != '{')
			p++;
		p++;
		while (*p != '{')
			p++;
		ret = json_scanf(p, strlen(p) - 1, "{ SOC:%f }", &p_charge);
		free(c);
		if (ret != 1)
			return -3;
	}

	charge = p_charge;
	akku = p_akku;
	grid = p_grid;
	load = p_load;
	pv = p_pv;

	return 0;
}

static int api() {
	res.len = 0;

	CURLcode ret = curl_easy_perform(curl);
	if (ret != CURLE_OK) {
		xerr("FRONIUS Error calling API: %d", ret);
		return -1;
	}

	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	if (http_code != 200) {
		xerr("FRONIUS API returned %d", http_code);
		return -2;
	}

	struct curl_header *header;
	CURLHcode hret = curl_easy_header(curl, "Content-Type", 0, CURLH_HEADER, -1, &header);
	if (hret != CURLHE_OK) {
		xerr("FRONIUS No Content-Type header from API: %d", hret);
		return -3;
	}

	// TODO validate application/json
	//	xlog("CURLHcode header value %s", header->value);
	//		if (header->value)
	//			return -4;

	return 0;
}

static int all_devices_max() {
	for (int i = 0; i < potd_size; i++) {
		device_t *d = potd[i];

		if (!d->active)
			continue;

		if (d->standby)
			continue;

		if (d->adjustable && d->power != 100)
			return 0;
		else if (!d->power)
			return 0;
	}

	return 1;
}

static int all_devices_off() {
	for (int i = 0; i < potd_size; i++) {
		device_t *d = potd[i];

		if (!d->active)
			continue;

		if (d->power)
			return 0;
	}

	return 1;
}

// if cloudy then we have alternating lighting conditions and therefore big distortion in PV production
static void calculate_distortion() {
	char message[128];
	char value[8];

	strcpy(message, "[");
	for (int i = 0; i < PV_HISTORY; i++) {
		snprintf(value, 8, "%d", history[i]);
		if (i > 0 && i < PV_HISTORY)
			strcat(message, ", ");
		strcat(message, value);
	}
	strcat(message, "]");

	int average = 0;
	for (int i = 0; i < PV_HISTORY; i++)
		average += history[i];
	average /= PV_HISTORY;

	int variation = 0;
	for (int i = 0; i < PV_HISTORY; i++)
		variation += abs(average - history[i]);

	distortion = variation > average;
	xlog("FRONIUS calculate_pv_distortion() %s average:%d variation:%d --> distortion:%d", message, average, variation, distortion);
}

// Grid < 0	--> upload
// Grid > 0	--> download

// Akku < 0	--> charge
// Akku > 0	--> discharge
static int calculate_steps(device_t *d) {
	int step = 0;

	if (d->greedy) {

		// single step to avoid swinging if surplus is around FROM and TO
		if ((KEEP_FROM - 50) < surplus && surplus < KEEP_FROM)
			return -1;
		else if (KEEP_FROM <= surplus && surplus < KEEP_TO)
			return 0;
		else if (KEEP_TO <= surplus && surplus < (KEEP_TO + 50))
			return 1;

		// surplus power steps
		step = surplus / (d->maximum / 100);

		if (distortion && step > 0)
			step /= 2;

		if (step < -100)
			step = -100; // min -100
		if (step > 100)
			step = 100; // max 100

	} else {

		// extra power steps
		step = extra / (d->maximum / 100);

		if (step < -100)
			step = -100; // min -100
		if (step > 100)
			step = 100; // max 100

		// smaller ramp up steps when we have distortion
		if (distortion && step > 0)
			step /= 2;

		// discharge when akku not full --> stop extra power
		if (charge < 99 && akku > 50)
			step = -100;
	}

	return step;
}

static int rampup_device(device_t *d) {
	if (!d->active)
		return 0; // continue loop

	if (d->standby)
		return 0; // continue loop

	int step = calculate_steps(d);

	// dumb devices can only be switched on or off
	if (!d->adjustable) {

		if (d->power)
			return 0; // already on - continue loop

		if (d->greedy) {

			// grid upload power + steal akku charge power
			if (surplus > d->maximum) {
				(d->set_function)(d, 1);
				return 1; // loop done
			}

		} else {

			// allow only grid upload power
			if (extra > d->maximum) {
				(d->set_function)(d, 1);
				return 1; // loop done
			}
		}
	}

	// check if device is ramped up to 100% but do not consume power
	// TODO funktioniert im sunny programm dann nicht mehr weil heizer an sind!
	if (d->power == 100 && (d->maximum / 2 * -1) < load) {
		d->standby = 1;
		(d->set_function)(d, STANDBY);
		return 0; // continue loop
	}

	if (d->power == 100)
		return 0; // already full up - continue loop

	if (step) {
		xlog("FRONIUS ramp↑ %s step %d", d->name, step);
		(d->set_function)(d, d->power + step);
		return 1; // loop done
	}

	return 0; // continue loop
}

static int rampdown_device(device_t *d) {
	int step = calculate_steps(d);

	// dumb devices can only be switched on or off
	if (!d->adjustable) {
		// switch off
		if (d->power) {
			(d->set_function)(d, 0);
			return 1; // loop done
		} else
			return 0; // already off - continue loop
	} else {
		xlog("FRONIUS ramp↓ %s step %d", d->name, step);
		(d->set_function)(d, d->power + step);
		return 0; // continue loop - as we don't know which one consumes power
	}
}

static void rampup() {
	// check if all devices already on
	if (all_devices_max()) {
		xlog("FRONIUS ramp↑ standby");
		wait = WAIT_STANDBY;
		return;
	}

	// ramp up devices
	for (int i = 0; i < potd_size; i++) {
		device_t *d = potd[i];
		if (rampup_device(d))
			return;
		else
			continue;
	}
}

static void rampdown() {
	// check if all devices already off
	if (all_devices_off()) {
		xlog("FRONIUS ramp↓ standby");
		wait = WAIT_STANDBY;
		return;
	}

	// ramp down devices in reverse order
	for (int i = 0; i < potd_size; i++) {
		device_t *d = potd[i];
		if (rampdown_device(d))
			return;
		else
			continue;
	}
}

static void* fronius(void *arg) {
	int ret, errors = 0, hour = -1, hour_forecast = -1;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	// do sunshine duration forecast and choose start program
	forecast_Rad1h();

	// switch off all
	set_devices(0);

	// the FRONIUS main loop
	while (1) {

		sleep(1);
		if (wait--)
			continue;

		// get actual date and time
		time_t now_ts = time(NULL);
		struct tm *now = localtime(&now_ts);

		// do sunshine duration forecast for new day and choose program
		if (now->tm_hour == 6 && hour_forecast == -1) {
			forecast_Rad1h();
			hour_forecast = now->tm_hour;
		} else
			hour_forecast = -1;

		// make Fronius API call
		ret = api();
		if (ret != 0) {
			xlog("FRONIUS api() returned %d", ret);
			if (++errors == 3)
				set_devices(0);
			wait = WAIT_NEXT;
			continue;
		} else
			errors = 0;

		// extract values from Fronius JSON response
		ret = parse();
		if (ret != 0) {
			xlog("FRONIUS parse() returned %d", ret);
			if (++errors == 3)
				set_devices(0);
			wait = WAIT_NEXT;
			continue;
		} else
			errors = 0;

		// not enough PV production, go into offline mode
		if (pv < 100) {
			xlog("FRONIUS Charge:%5d Akku:%5d Grid:%5d Load:%5d PV:%5d --> offline", charge, akku, grid, load, pv);
			surplus = extra = 0;
			set_devices(0);
			wait = WAIT_OFFLINE;
			continue;
		}

		// default wait for next round
		wait = WAIT_KEEP;

		// update PV history
		history[history_ptr++] = pv;
		if (history_ptr == PV_HISTORY)
			history_ptr = 0;

		// reset device states once per hour
		if (hour != now->tm_hour) {
			hour = now->tm_hour;
			xlog("FRONIUS resetting all device states");
			set_devices(0);
		}

		// akku charge + grid upload
		surplus = (grid + akku) * -1;

		// grid upload - extra power from secondary inverters
		extra = grid * -1;
		if (extra < 25)
			extra = 0;

		// do smaller steps when we have distortion
		calculate_distortion();

		xlog("FRONIUS Charge:%5d Akku:%5d Grid:%5d Load:%5d PV:%5d Surplus:%5d Extra:%5d", charge, akku, grid, load, pv, surplus, extra);

		if (surplus < KEEP_FROM)
			// consuming grid power or discharging akku: ramp down
			rampdown();
		else if (surplus > KEEP_TO)
			// uploading grid power or charging akku: ramp up
			rampup();

		// faster next round when distortion
		if (distortion && wait > 10)
			wait /= 2;

		print_status();
	}
}

// Kalibrierung über SmartMeter mit Laptop im Akku-Betrieb:
// - Nur Nachts
// - Akku aus
// - Külschränke aus
// - Heizung aus
// - Rechner aus
static void calibrate(char *name) {
	char message[16];
	float p_power;
	int voltage, closest, target, offset_start = 0, offset_end = 0;
	int measure[1000], raster[101];

	// create a dummy device
	device_t boiler = { .name = name, .addr = resolve_ip(name) };

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	// write IP and port into sockaddr structure
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(boiler.addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	curl = curl_easy_init();
	if (curl == NULL)
		perror("Error initializing libcurl");

	curl_easy_setopt(curl, CURLOPT_URL, URL_METER);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "http");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void* ) &res);

	printf("starting calibration on %s (%s)\n", boiler.name, boiler.addr);
	snprintf(message, 16, "0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// average offset power at start
	printf("calculating offset start");
	for (int i = 0; i < 10; i++) {
		res.len = 0;
		curl_easy_perform(curl);
		json_scanf(res.buffer, res.len, "{ Body { Data { PowerReal_P_Sum:%f } } }", &p_power);
		offset_start += (int) p_power;
		printf(" %d", (int) p_power);
		sleep(1);
	}
	offset_start /= 10;
	printf(" --> average %d\n", offset_start);

	printf("waiting for heat up 100%%...\n");
	snprintf(message, 16, "10000:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// get maximum power
	res.len = 0;
	curl_easy_perform(curl);
	json_scanf(res.buffer, res.len, "{ Body { Data { PowerReal_P_Sum:%f } } }", &p_power);
	int max_power = round100(((int) p_power) - offset_start);

	int onepercent = max_power / 100;
	printf("starting measurement with maximum power %d watt 1%%=%d watt\n", max_power, onepercent);

	// do a full drive over SSR characteristic load curve from 10 down to 0 volt and capture power
	for (int i = 0; i < 1000; i++) {
		voltage = 10000 - (i * 10);

		snprintf(message, 16, "%d:%d", voltage, 0);
		sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));

		// give SSR time to set voltage and smart meter to measure
		if (2000 < voltage && voltage < 8000)
			usleep(1000 * 1000); // more time between 8 and 2 volts
		else
			usleep(1000 * 600);

		res.len = 0;
		curl_easy_perform(curl);
		json_scanf(res.buffer, res.len, "{ Body { Data { PowerReal_P_Sum:%f } } }", &p_power);

		measure[i] = ((int) p_power) - offset_start;
		printf("%5d %5d\n", voltage, measure[i]);
	}

	// build raster table
	raster[0] = 10000;
	raster[100] = 0;
	for (int i = 1; i < 100; i++) {

		// calculate next target power -i%
		target = max_power - (onepercent * i);

		// find closest power to target power
		int min_diff = max_power;
		for (int j = 0; j < 1000; j++) {
			int diff = abs(measure[j] - target);
			if (diff < min_diff) {
				min_diff = diff;
				closest = j;
			}
		}

		// find all closest voltages that match target power
		int sum = 0, count = 0;
		printf("closest voltages to target power %5d matching %5d: ", target, measure[closest]);
		for (int j = 0; j < 1000; j++)
			if (measure[j] == measure[closest]) {
				printf("%5d", j);
				sum += 10000 - (j * 10);
				count++;
			}

		// average of all closest voltages
		raster[i] = sum / count;

		printf(" --> average %5d\n", raster[i]);
	}

	// average offset power at end
	printf("calculating offset end");
	for (int i = 0; i < 10; i++) {
		res.len = 0;
		curl_easy_perform(curl);
		json_scanf(res.buffer, res.len, "{ Body { Data { PowerReal_P_Sum:%f } } }", &p_power);
		offset_end += (int) p_power;
		printf(" %d", (int) p_power);
		sleep(1);
	}
	offset_end /= 10;
	printf(" --> average %d\n", offset_end);

	// validate - values in measure table should shrink, not grow
	for (int i = 1; i < 1000; i++)
		if (measure[i - 1] < (measure[i] - 5)) { // with 5 watt tolerance
			int v_x = 10000 - (i * 10);
			int m_x = measure[i - 1];
			int v_y = 10000 - ((i - 1) * 10);
			int m_y = measure[i];
			printf("!!! WARNING !!! measuring tainted with parasitic power at voltage %d:%d < %d:%d\n", v_x, m_x, v_y, m_y);
		}
	if (offset_start != offset_end)
		printf("!!! WARNING !!! measuring tainted with parasitic power between start and end\n");

	// dump raster table in ascending order
	printf("phase angle voltage table 0..100%% in %d watt steps:\n\n", onepercent);
	printf("%d, ", raster[100]);
	for (int i = 99; i >= 0; i--) {
		printf("%d, ", raster[i]);
		if (i % 10 == 0)
			printf("\\\n");
	}

	// cleanup
	close(sock);
	free(res.buffer);
	curl_easy_cleanup(curl);
}

static int init() {
	// initialize all devices with start values
	for (int i = 0; i < ARRAY_SIZE(CONFIG_CLOUDY); i++) {
		device_t *d = CONFIG_CLOUDY[i];
		d->active = 1;
		d->power = -1;
		d->addr = resolve_ip(d->name);
	}
	for (int i = 0; i < ARRAY_SIZE(CONFIG_SUNNY); i++) {
		device_t *d = CONFIG_SUNNY[i];
		d->active = 1;
		d->power = -1;
		d->addr = resolve_ip(d->name);
	}

	// debug phase angle edges
	for (int i = 0; i < ARRAY_SIZE(CONFIG_CLOUDY); i++) {
		device_t *d = CONFIG_CLOUDY[i];
		if (d->phase_angle != NULL)
			xlog("FRONIUS %s 0=%d, 1=%d, 50=%d, 100=%d", d->name, d->phase_angle[0], d->phase_angle[1], d->phase_angle[50], d->phase_angle[100]);
	}

	curl = curl_easy_init();
	if (curl == NULL)
		return xerr("Error initializing libcurl");

	curl_easy_setopt(curl, CURLOPT_URL, URL_FLOW);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "http");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void* ) &res);

	if (pthread_create(&thread, NULL, &fronius, NULL))
		return xerr("Error creating fronius thread");

	xlog("FRONIUS initialized");
	return 0;
}

static void stop() {
	if (pthread_cancel(thread))
		xlog("Error canceling fronius thread");

	if (pthread_join(thread, NULL))
		xlog("Error joining fronius thread");

	// stop and destroy this module
	free(res.buffer);
	curl_easy_cleanup(curl);

	if (sock != 0)
		close(sock);
}

void fronius_override(const char *name) {
	for (int i = 0; i < potd_size; i++) {
		device_t *d = potd[i];

		if (!strcmp(d->name, name)) {
			d->override = pv < 100 ? 1 : 120; // WAIT_OFFLINE x 1 or WAIT_KEEP x 120
			d->active = 1;
			d->standby = 0;
			xlog("FRONIUS Setting Override for %s loops %d", d->name, d->override);
			(d->set_function)(d, 100);
		}
	}
}

int fronius_main(int argc, char **argv) {

	// no arguments - main loop
	if (argc == 1) {
		init();
		pause();
		stop();
		return 0;
	}

	int i;
	char *c = NULL;
	while ((i = getopt(argc, argv, "c:")) != -1) {
		switch (i) {
		case 'c':
			c = optarg;
			break;
		}
	}

	// calibration - execute as
	//   stdbuf -i0 -o0 -e0 ./fronius -c boiler1 > boiler1.txt
	if (c != NULL)
		calibrate(c);

	return 0;
}

#ifdef FRONIUS_MAIN
int main(int argc, char **argv) {
	return fronius_main(argc, argv);
}
#else
MCP_REGISTER(fronius, 7, &init, &stop);
#endif
