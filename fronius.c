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

#include "fronius.h"
#include "frozen.h"
#include "utils.h"
#include "mcp.h"

#define FRONIUSLOG 			"FRONIUS Charge:%5d Akku:%5d Grid:%5d Load:%5d PV:%5d Surplus:%5d Step:%3d"

static pthread_t thread;

// curl response buffer
static CURL *curl;
static get_response_t res = { .buffer = NULL, .len = 0, .buflen = CHUNK_SIZE };

// device status for boilers and heaters
// static const char *devices[] = { BOILERS, HEATERS };
static const char *devices[] = { HEATERS, BOILERS };
static device_t **device;

// TODO
// config_sunny
// config_cloudy

// actual Fronius power flow data + calculations
static int charge, akku, grid, load, pv, surplus, surplus_step, extra, extra_step, distortion;

// PV history values to calculate distortion
static int history[PV_HISTORY];
static int history_ptr = 0;

// SSR control voltage for 0..100% power
// TODO Tabelle in ESP32 integrieren und direktaufruf zusätzlich über prozentuale angabe
static const unsigned int phase_angle1[] = { PHASE_ANGLES_BOILER1 };
static const unsigned int phase_angle2[] = { PHASE_ANGLES_BOILER2 };
static const unsigned int phase_angle3[] = { PHASE_ANGLES_BOILER3 };

// 1% of maximum boiler power
static const int percent = BOILER_WATT / 100;

static int wait = WAIT_NEXT;
static int sock = 0;

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
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = device[i];

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
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = device[i];

		if (!d->active)
			continue;

		if (d->power)
			return 0;
	}

	return 1;
}

static void set_devices(int power) {
	for (int i = 0; i < ARRAY_SIZE(devices); i++)
		(device[i]->set_function)(i, power);
}

static int set_heater(int index, int power) {
	device_t *heater = device[index];

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

static int set_boiler(int index, int power) {
	device_t *boiler = device[index];

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
	if (boiler->standby && power > BOILER_STANDBY)
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

static void print_status() {
	char message[128];
	char value[5];

	strcpy(message, "FRONIUS devices active ");
	for (int i = 0; i < ARRAY_SIZE(devices); i++)
		strcat(message, device[i]->active ? "1" : "0");

	strcat(message, "   power ");
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		snprintf(value, 5, " %3d", device[i]->power);
		strcat(message, value);
	}

	snprintf(value, 5, "%3d", wait);
	strcat(message, "   wait ");
	strcat(message, value);
	xlog(message);
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
static void calculate_steps() {

	// akku charge + grid upload
	surplus = (grid + akku) * -1;

	// single step to avoid swinging if surplus is between 0 1nd 100
	if (-25 < surplus && surplus < 25) {
		surplus_step = -1;
		return;
	} else if (25 <= surplus && surplus < 75) {
		surplus_step = 0;
		return;
	} else if (75 <= surplus && surplus < 100) {
		surplus_step = 1;
		return;
	}

	// next step
	surplus_step = surplus / percent;

	// smaller ramp up steps when we have distortion
	calculate_distortion();
	if (distortion && surplus_step > 0)
		surplus_step /= 2;

	if (surplus_step < -100)
		surplus_step = -100; // min -100
	if (surplus_step > 100)
		surplus_step = 100; // max 100

	// grid upload - extra power from secondary inverters
	extra = grid * -1;
	if (-50 < extra && extra < 10)
		extra = 0;

	// extra power steps
	extra_step = extra / percent;

	if (extra_step < -100)
		extra_step = -100; // min -100
	if (extra_step > 100)
		extra_step = 100; // max 100

	// smaller ramp up steps when we have distortion
	if (distortion && extra_step > 0)
		extra_step /= 2;

	// discharge when akku not full --> stop extra power
	if (charge < 99 && akku > 50)
		extra_step = -100;
}

static void rampup() {
	// check if all devices already on
	if (all_devices_max()) {
		xlog(FRONIUSLOG" --> ramp↑ standby", charge, akku, grid, load, pv, surplus, surplus_step);
		wait = WAIT_STANDBY;
		return;
	}

	xlog(FRONIUSLOG" --> ramp↑", charge, akku, grid, load, pv, surplus, surplus_step);

	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = device[i];

		if (!d->active)
			continue;

		if (d->standby)
			continue;

		// dumb devices can only be switched on or off
		if (!d->adjustable) {

			if (d->power)
				continue; // already on

			if (d->greedy) {

				// grid upload power + steal akku charge power
				if (surplus > HEATER_WATT) {
					(d->set_function)(i, 1);
					return;
				}

			} else {

				// allow only grid upload power
				if (extra > HEATER_WATT) {
					(d->set_function)(i, 1);
					return;
				}

			}
			continue;
		}

		// check if device is ramped up to 100% but do not consume power
		// TODO funktioniert im sunny programm dann nicht mehr weil heizer an sind!
		if (d->power == 100 && (BOILER_WATT / 2 * -1) < load) {
			d->standby = 1;
			(d->set_function)(i, BOILER_STANDBY);
			continue;
		}

		// ramp up each device separately
		if (d->power != 100) {

			if (d->greedy) {

				// grid upload power + steal akku charge power
				if (surplus_step) {
					(d->set_function)(i, d->power + surplus_step);
					return;
				}

			} else {

				// allow only grid upload power
				if (extra_step) {
					xlog("FRONIUS adjusting extra power %d watt on %s step %d", extra, d->name, extra_step);
					(d->set_function)(i, d->power + extra_step);
					return;
				}
			}
		}
	}
}

static void rampdown() {
	// check if all devices already off
	if (all_devices_off()) {
		xlog(FRONIUSLOG" --> ramp↓ standby", charge, akku, grid, load, pv, surplus, surplus_step);
		wait = WAIT_STANDBY;
		return;
	}

	xlog(FRONIUSLOG" --> ramp↓", charge, akku, grid, load, pv, surplus, surplus_step);

	// reverse order
	for (int i = ARRAY_SIZE(devices) - 1; i >= 0; i--) {
		device_t *d = device[i];

		// dumb devices can only be switched on or off
		if (!d->adjustable) {

			// switch off
			if (d->power) {
				(d->set_function)(i, 0);
				return;
			}

			continue;
		}

		// lowering all devices - as we don't know which one consumes power
		(d->set_function)(i, d->power + surplus_step);
	}
}

static void* fronius(void *arg) {
	int ret, day = -1, hour = -1;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	// switch off all
	set_devices(0);

	while (1) {

		sleep(1);
		if (wait--)
			continue;

		// make Fronius API call
		ret = api();
		if (ret != 0) {
			xlog("FRONIUS api() returned %d", ret);
			set_devices(0);
			wait = WAIT_KEEP;
			continue;
		}

		// extract values from Fronius JSON response
		ret = parse();
		if (ret != 0) {
			xlog("FRONIUS parse() returned %d", ret);
			set_devices(0);
			wait = WAIT_KEEP;
			continue;
		}

		// not enough PV production, go into offline mode
		if (pv < 100) {
			xlog(FRONIUSLOG" --> offline", charge, akku, grid, load, pv, surplus, surplus_step);
			surplus_step = surplus = 0;
			set_devices(0);
			wait = WAIT_OFFLINE;
			continue;
		}

		// get actual date and time
		time_t now_ts = time(NULL);
		struct tm *now = localtime(&now_ts);

		// reset device states once per hour
		if (hour != now->tm_hour) {
			hour = now->tm_hour;
			xlog("FRONIUS resetting all device states");
			set_devices(0);
		}

		// TODO
		// do weather forecast for tody and choose program
		if (day != now->tm_mday) {
			day = now->tm_mday;
			xlog("FRONIUS choosing program from weather forecast");
			// forecast();
		}

		// update PV history
		history[history_ptr++] = pv;
		if (history_ptr == PV_HISTORY)
			history_ptr = 0;

		// default wait for next round
		wait = WAIT_KEEP;

		// convert surplus+extra power into ramp up / ramp down percent steps
		calculate_steps();

		if (surplus_step < 0)
			// consuming grid power or discharging akku: ramp down
			rampdown();
		else if (surplus_step > 0)
			// uploading grid power or charging akku: ramp up
			rampup();
		else
			// keep current state
			xlog(FRONIUSLOG" --> keep", charge, akku, grid, load, pv, surplus, surplus_step);

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
	// create device structures for boilers and heaters
	device = malloc(ARRAY_SIZE(devices));

	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = malloc(sizeof(device_t));
		ZERO(d);

		d->active = 1;
		d->power = -1;
		d->name = devices[i];
		d->addr = resolve_ip(d->name);

		switch (i) {
		case 0:
			d->greedy = 1;
			d->set_function = &set_heater;
			break;
		case 1:
			d->adjustable = 1;
			d->greedy = 1;
			d->phase_angle = phase_angle1;
			d->set_function = &set_boiler;
			break;
		case 2:
			d->adjustable = 1;
			d->greedy = 1;
			d->phase_angle = phase_angle2;
			d->set_function = &set_boiler;
			break;
		case 3:
			d->adjustable = 1;
			d->phase_angle = phase_angle3;
			d->set_function = &set_boiler;
			break;
		}

		device[i] = d;
	}

	// debug phase angle edges
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = device[i];
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

void fronius_override(int index) {
	if (index < 0 || index >= ARRAY_SIZE(devices))
		return;

	device_t *d = device[index];
	d->override = pv < 100 ? 1 : 120; // WAIT_OFFLINE x 1 or WAIT_KEEP x 120
	d->active = 1;
	d->standby = 0;

	xlog("FRONIUS Setting Override for %s loops %d", d->name, d->override);
	(d->set_function)(index, 100);
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
