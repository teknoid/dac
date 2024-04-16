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

// program of the day - mosmix will chose appropriate program
static potd_t *potd;

// actual Fronius power flow data and calculations
static int akku, grid, load, pv, chrg, pv7, last_load, distortion, tendence, kf, kt;

// PV history values to calculate distortion
static int history[PV_HISTORY];
static int history_ptr = 0;

static char line[LINEBUF];
static int wait = 0, sock = 0;

static pthread_t thread;

static struct tm *now;

int set_heater(device_t *heater, int power) {
	// fix power value if out of range
	if (power < 0)
		power = 0;
	if (power > 1)
		power = 1;

	// check if update is necessary
	if (heater->power == power)
		return 0;

	if (!heater->active)
		power = 0;

	// can we send a message
	if (heater->addr == NULL)
		return xerr("No address to send HTTP message");

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

	return 1; // loop done
}

int set_boiler(device_t *boiler, int power) {
	// fix power value if out of range
	if (power < 0)
		power = 0;
	if (power > 100)
		power = 100;

	// can we send a message
	if (boiler->addr == NULL)
		return xerr("No address to send UDP message");

	if (boiler->override) {
		time_t t = time(NULL);
		if (t > boiler->override) {
			xdebug("FRONIUS Override expired for %s", boiler->name);
			boiler->override = 0;
			power = 0;
		} else {
			xdebug("FRONIUS Override active for %lu seconds on %s", boiler->override - t, boiler->name);
			power = 100;
		}
	} else {
		if (!boiler->active)
			power = 0;
		if (power == 0)
			boiler->standby = 0; // zero resets standby
	}

	// no update necessary
	if (boiler->power == power)
		return 0;

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
	snprintf(message, 16, "p:%d:%d", power, 0);

	// send message to boiler
	if (sendto(sock, message, strlen(message), 0, sa, sizeof(*sa)) < 0)
		return xerr("Sendto failed");

	int step = power - boiler->power;
	if (step > 0)
		xdebug("FRONIUS ramp↑ %s step +%d UDP %s", boiler->name, step, message);
	else
		xdebug("FRONIUS ramp↓ %s step %d UDP %s", boiler->name, step, message);

	// update power value
	boiler->power = power;
	wait = WAIT_NEXT;

	return 1; //loop done
}

static int collect_dumb_load() {
	int dumb_load = 0;
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = devices[i];
		if (!d->adjustable && d->power)
			dumb_load += d->load;
	}
	return dumb_load * -1;
}

static int collect_adjustable_power() {
	int adj_power = 0, greedy_dumb_off = 0;

	// collect non greedy adjustable power
	for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++)
		if (!(*ds)->greedy && (*ds)->device->adjustable)
			adj_power += (*ds)->device->power * (*ds)->device->load / 100;

	// check if we have greedy dumb off devices
	for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++)
		if ((*ds)->greedy && !(*ds)->device->adjustable && !(*ds)->device->power)
			greedy_dumb_off = 1;

	// a greedy dumb off device can steal power from a non greedy adjustable device which is ramped up
	int xpower = greedy_dumb_off ? adj_power : 0;
	xdebug("FRONIUS collect_adjustable_power() %d (%d)", xpower, adj_power);
	return xpower;
}

static void set_all_devices(int power) {
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = devices[i];
		(d->set_function)(d, power);
	}
}

// initialize all devices with start values
static void init_all_devices() {
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = devices[i];
		d->active = 1;
		d->power = -1;
		d->addr = resolve_ip(d->name);
	}
}

static void print_power_status(int surplus, int extra, int sum, const char *message) {
	xlogl_start(line, "FRONIUS");
	xlogl_int_b(line, "PV", pv + pv7);
	xlogl_int(line, 1, 1, "Grid", grid);
	xlogl_int(line, 1, 1, "Akku", akku);
	if (surplus != 0)
		xlogl_int(line, 1, 0, "Surplus", surplus);
	if (extra != 0)
		xlogl_int(line, 1, 0, "Extra", extra);
	xlogl_int(line, 0, 0, "Chrg", chrg);
	xlogl_int(line, 0, 0, "Load", load);
	xlogl_int(line, 0, 0, "last Load", last_load);
	xlogl_int(line, 0, 0, "PV10", pv);
	xlogl_int(line, 0, 0, "PV7", pv7);
	if (sum != 0)
		xlogl_int(line, 0, 0, "Sum", sum);

	xlogl_end(line, message);
}

static void print_device_status() {
	char message[128];
	char value[5];

	strcpy(message, "FRONIUS devices active ");
	for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++)
		strcat(message, (*ds)->device->active ? "1" : "0");

	strcat(message, "   power ");
	for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++) {
		snprintf(value, 5, " %3d", (*ds)->device->power);
		strcat(message, value);
	}

	snprintf(value, 5, "%3d", wait);
	strcat(message, "   wait ");
	strcat(message, value);
	xlog(message);
}

static CURL* curl_init(const char *url, _curl_write_callback2 cb) {
	CURL *curl = curl_easy_init();
	if (curl != NULL) {
		curl_easy_setopt(curl, CURLOPT_URL, url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
		curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 4096);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 30L);
		curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
	}
	return curl;
}

static int curl_perform(CURL *curl) {
	CURLcode ret = curl_easy_perform(curl);
	if (ret != CURLE_OK)
		return xerrr(-1, "FRONIUS curl perform error %d", ret);

	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	if (http_code != 200)
		return xerrr(-2, "FRONIUS response code %d", http_code);

	return 0;
}

static size_t callback_fronius10(const char *data, size_t size, size_t nmemb, const void *userdata) {
	size_t realsize = size * nmemb;
	xdebug("FRONIUS callback_fronius10() size %d nmemb %d realsize %d", size, nmemb, realsize);

	if (nmemb < 32)
		return realsize; // noise

	float p_charge, p_akku, p_grid, p_load, p_pv;
	char *c;
	int ret;

	ret = json_scanf(data, realsize, "{ Body { Data { Site { P_Akku:%f, P_Grid:%f, P_Load:%f, P_PV:%f } } } }", &p_akku, &p_grid, &p_load, &p_pv);
	if (ret != 4)
		xlog("FRONIUS callback_fronius10() warning! parsing Body->Data->Site: expected 4 values but got only %d", ret);

	akku = p_akku;
	grid = p_grid;
	load = p_load;
	pv = p_pv;

	// workaround parsing { "Inverters" : { "1" : { ... } } }
	ret = json_scanf(data, realsize, "{ Body { Data { Inverters:%Q } } }", &c);
	if (ret == 1 && c != NULL) {
		char *p = c;
		while (*p != '{')
			p++;
		p++;
		while (*p != '{')
			p++;

		ret = json_scanf(p, strlen(p) - 1, "{ SOC:%f }", &p_charge);
		if (ret == 1)
			chrg = p_charge;
		else {
			xlog("FRONIUS callback_fronius10() warning! parsing Body->Data->Inverters->SOC: no result");
			chrg = 0;
		}

		free(c);
	} else
		xlog("FRONIUS callback_fronius10() warning! parsing Body->Data->Inverters: no result");

	return realsize;
}

static size_t callback_fronius7(const char *data, size_t size, size_t nmemb, const void *userdata) {
	size_t realsize = size * nmemb;
	xdebug("FRONIUS callback_fronius7() size %d nmemb %d realsize %d", size, nmemb, realsize);

	if (nmemb < 32)
		return realsize; // noise

	float p_pv;
	int ret = json_scanf(data, realsize, "{ Body { Data { Site { P_PV:%f } } } }", &p_pv);
	if (ret == 1)
		pv7 = p_pv;
	else {
		xlog("FRONIUS callback_fronius7() warning! parsing Body->Data->Site->P_PV: no result");
		pv7 = 0;
	}

	return realsize;
}

static size_t callback_calibrate(const char *data, size_t size, size_t nmemb, const void *userdata) {
	size_t realsize = size * nmemb;
	xdebug("FRONIUS callback_calibrate() size %d nmemb %d realsize %d", size, nmemb, realsize);

	if (nmemb < 32)
		return realsize; // noise

	float p_grid;
	int ret = json_scanf(data, realsize, "{ Body { Data { PowerReal_P_Sum:%f } } }", &p_grid);
	if (ret == 1)
		grid = p_grid;
	else {
		xlog("FRONIUS callback_calibrate() warning! parsing Body->Data->PowerReal_P_Sum: no result");
		grid = 0;
	}

	return realsize;
}

static int get_history(int offset) {
	int i = history_ptr + offset;
	if (i < 0)
		i += PV_HISTORY;
	if (i >= PV_HISTORY)
		i -= PV_HISTORY;
	return history[i];
}

static void update_history() {
	char message[PV_HISTORY * 8 + 2];
	char value[8];

	history[history_ptr++] = pv;
	if (history_ptr == PV_HISTORY)
		history_ptr = 0;

	strcpy(message, "[");
	for (int i = 0; i < PV_HISTORY; i++) {
		snprintf(value, 8, "%d", history[i]);
		if (i > 0 && i < PV_HISTORY)
			strcat(message, ", ");
		strcat(message, value);
	}
	strcat(message, "]");

	// calculate average
	int average = 0;
	for (int i = 0; i < PV_HISTORY; i++)
		average += history[i];
	average /= PV_HISTORY;

	// calculate variation
	unsigned long variation = 0;
	for (int i = 0; i < PV_HISTORY; i++)
		variation += abs(average - history[i]);

	// grade of alternation in pv production when its cloudy with sunny gaps
	distortion = variation / average;

	// calculate tendence
	int h0 = get_history(-1);
	int h1 = get_history(-2);
	int h2 = get_history(-3);
	if (h2 < h1 && h1 < h0)
		tendence = 1; // pv is raising
	else if (h2 > h1 && h1 > h0)
		tendence = -1; // pv is falling
	else
		tendence = 0;

	// allow more tolerance for big pv production
	int tolerance = pv / 1000 + 1;
	kf = KEEP_FROM * tolerance;
	kt = KEEP_TO * tolerance;

	xlog("FRONIUS %s avg:%d var:%lu dist:%d tend:%d kf:%d kt:%d", message, average, variation, distortion, tendence, kf, kt);
}

static int choose_program(const potd_t *p) {
	xlog("FRONIUS choosing %s program of the day", p->name);
	potd = (potd_t*) p;
	return 0;
}

static int mosmix() {
	char line[8];
	int today, tomorrow, tomorrowplus1;

	// default program
	potd = (potd_t*) &CLOUDY_EMPTY;

	FILE *fp = fopen(MOSMIX, "r");
	if (fp == NULL)
		return xerr("FRONIUS no mosmix data available");

	if (fgets(line, 8, fp) != NULL)
		if (sscanf(line, "%d", &today) != 1)
			return xerr("FRONIUS mosmix parse error %s", line);

	if (fgets(line, 8, fp) != NULL)
		if (sscanf(line, "%d", &tomorrow) != 1)
			return xerr("FRONIUS mosmix parse error %s", line);

	if (fgets(line, 8, fp) != NULL)
		if (sscanf(line, "%d", &tomorrowplus1) != 1)
			return xerr("FRONIUS mosmix parse error %s", line);

	fclose(fp);

	int exp_today = today * MOSMIX_FACTOR;
	int exp_tom = tomorrow * MOSMIX_FACTOR;
	int exp_tomp1 = tomorrowplus1 * MOSMIX_FACTOR;
	int needed = SELF_CONSUMING - SELF_CONSUMING * now->tm_hour / 24 + AKKU_CAPACITY - AKKU_CAPACITY * chrg / 100;

	xlog("FRONIUS mosmix needed %d, Rad1h/expected today %d/%d tomorrow %d/%d tomorrow+1 %d/%d", needed, today, exp_today, tomorrow, exp_tom, tomorrowplus1, exp_tomp1);

	if (exp_today > needed)
		return choose_program(&SUNNY);

	if (chrg > 50 && exp_tom > SELF_CONSUMING)
		return choose_program(&TOMORROW);

	if (chrg > 75)
		return choose_program(&CLOUDY_FULL);

	return choose_program(&CLOUDY_EMPTY);
}

static int calculate_step(device_t *d, int power) {
	// power steps
	int step = power / (d->load / 100);
	xdebug("FRONIUS step1 %d", step);

	// we need at least one step if power is not null
	if (!step) {
		if (power < 0)
			step = tendence < 0 ? -2 : -1;
		else if (power > 0)
			step = tendence > 0 ? 2 : 1;
		xdebug("FRONIUS step2 %d", step);
		return step;
	}

	// when we have distortion, do: smaller up steps / bigger down steps
	if (distortion) {
		if (step > 0)
			step /= (distortion == 1 ? 2 : distortion);
		else
			step *= (distortion == 1 ? 2 : distortion);
		xdebug("FRONIUS step3 %d", step);
	}

	if (step < -100)
		step = -100; // min -100
	if (step > 100)
		step = 100; // max 100
	xdebug("FRONIUS step4 %d", step);

	return step;
}

// check if device is ramped up to 100% but does not consume power
static int check_standby(device_t *d, int power) {
	int threshold = d->load / 2 * -1;
	int diff = last_load - load;
	int no_load = load > threshold; // no load at all
	int switched_off = diff < threshold; // thermostat switched off: now at least half less load than before
	if (!d->override && !d->standby && (no_load || switched_off)) {
		d->standby = 1;
		(d->set_function)(d, STANDBY);
		xdebug("FRONIUS %s: load %d, diff %d, threshold %d --> entering standby", d->name, load, diff, threshold);
	}
	return 0; // always continue loop
}

static int ramp_adjustable(device_t *d, int power) {
	xdebug("FRONIUS ramp_adjustable %s %d", d->name, power);
	int step = calculate_step(d, power);

	if (step == 0)
		return 0; // continue loop
	else if (step > 0 && d->power == 100)
		return check_standby(d, power);
	else
		return (d->set_function)(d, d->power + step);
}

static int ramp_dumb(device_t *d, int power) {
	xdebug("FRONIUS ramp_dumb %s %d", d->name, power);

	// keep on as long as we have enough power and device is already on
	if (d->power && power > 0)
		return 0; // continue loop

	// switch on when enough power is available
	if (!d->power && power > d->load)
		return (d->set_function)(d, 1);

	// switch off
	if (d->power)
		return (d->set_function)(d, 0);

	return 0; // continue loop
}

static int rampup_device(device_t *d, int power) {
	xdebug("FRONIUS rampup_device %s %d", d->name, power);

	if (!d->active)
		return 0; // continue loop

	if (d->standby)
		return 0; // continue loop

	if (d->adjustable)
		return ramp_adjustable(d, power);
	else
		return ramp_dumb(d, power);
}

static int rampdown_device(device_t *d, int power) {
	xdebug("FRONIUS rampdown_device %s %d", d->name, power);

	if (!d->power)
		return 0; // already off - continue loop

	if (d->adjustable)
		return ramp_adjustable(d, power);
	else
		return ramp_dumb(d, power);
}

static int rampup(int power, int only_greedy) {
	for (const potd_device_t **ds = potd->devices; *ds != NULL; ds++) {
		if (only_greedy && !(*ds)->greedy)
			continue; // skip non-greedy devices
		if (rampup_device((*ds)->device, power))
			return 1;
	}

	return 0; // next priority
}

static int rampdown(int power, int skip_greedy) {
	const potd_device_t **ds = potd->devices;

	// jump to last entry
	while (*ds != NULL)
		ds++;
	ds--;

	// now go backward - this will give a reverse order
	do {
		if (skip_greedy && (*ds)->greedy)
			continue; // skip greedy devices
		if (rampdown_device((*ds)->device, power))
			return 1;
	} while (ds-- != potd->devices);

	return 0; // next priority
}

// do device adjustments in sequence of priority
static void ramp(int surplus, int extra) {
	// 1. no extra power available: ramp down devices but skip greedy
	if (extra < kf)
		if (rampdown(extra - kf, 1))
			return;

	// 2. consuming grid power or discharging akku: ramp down all devices
	if (surplus < kf)
		if (rampdown(surplus - kf, 0))
			return;

	// steal power from ramped adjustable devices for greedy dumb devices
	surplus += collect_adjustable_power();

	// 3. uploading grid power or charging akku: ramp up only greedy devices
	if (surplus > kt)
		if (rampup(surplus - kt, 1))
			return;

	// 4. extra power available: ramp up all devices
	if (extra > kt)
		if (rampup(extra - kt, 0))
			return;
}

static void* fronius(void *arg) {
	int ret, surplus, extra, sum, errors = 0, hour = -1;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	CURL *curl10 = curl_init(URL_FLOW10, callback_fronius10);
	if (curl10 == NULL) {
		xlog("Error initializing libcurl");
		return (void*) 0;
	}

	CURL *curl7 = curl_init(URL_FLOW7, callback_fronius7);
	if (curl7 == NULL) {
		xlog("Error initializing libcurl");
		return (void*) 0;
	}

	// the FRONIUS main loop
	while (1) {

		sleep(1);
		if (wait--)
			continue;

		// check error counter
		if (errors == 3)
			set_all_devices(0);

		// get actual date and time
		time_t now_ts = time(NULL);
		now = localtime(&now_ts);

		// make Fronius10 API call
		ret = curl_perform(curl10);
		if (ret != 0) {
			xlog("FRONIUS Error calling Fronius10 API: %d", ret);
			errors++;
			wait = WAIT_NEXT;
			continue;
		}

		// not enough PV production, go into offline mode
		if (pv < 100) {
			print_power_status(0, 0, 0, "--> offline");
			set_all_devices(0);
			wait = WAIT_OFFLINE;
			pv7 = 0;
			continue;
		}

		// reset program of the day and device states once per hour
		if (potd == NULL || hour != now->tm_hour) {
			xlog("FRONIUS resetting all device states");
			set_all_devices(0);
			mosmix();
			hour = now->tm_hour;
			wait = WAIT_NEXT;
			continue;
		}

		// make Fronius7 API call
		ret = curl_perform(curl7);
		if (ret != 0)
			xlog("FRONIUS Error calling Fronius7 API: %d", ret);

		// recalculate load
		int raw_load = load;
		int dumb_load = collect_dumb_load();
		load -= dumb_load; // subtract load consumed by dumb devices
		load -= pv7; // subtract PV produced by Fronius7
		xdebug("FRONIUS recalculate load: raw_load %d, dumb_load %d, PV7 %d, real_load %d", raw_load, dumb_load, pv7, load);

		// update PV history
		update_history();

		// grid < 0	--> upload
		// grid > 0	--> download

		// akku < 0	--> charge
		// akku > 0	--> discharge

		// surplus = akku charge + grid upload
		surplus = (grid + akku) * -1;

		// extra = grid upload - not going into akku or from secondary inverters
		extra = grid * -1;

		// discharging akku is not extra power
		if (akku > 0)
			extra -= akku;

		sum = grid + akku + load + pv;
		print_power_status(surplus, extra, sum, NULL);

		// default wait for next round
		wait = WAIT_KEEP;

		// ramp up /ramp down devices depending on surplus + extra power
		ramp(surplus, extra);

		// faster next round when we have distortion
		if (distortion && wait > 10)
			wait /= 2;

		// much faster next round on grid load or extreme distortion
		if (grid > 25 || distortion > 5)
			wait = WAIT_NEXT;

		print_device_status();
		last_load = load;
		errors = 0;
	}
}

// Kalibrierung über SmartMeter mit Laptop im Akku-Betrieb:
// - Nur Nachts
// - Akku aus
// - Külschränke aus
// - Heizung aus
// - Rechner aus
static int calibrate(char *name) {
	const char *addr = resolve_ip(name);
	char message[16];
	int voltage, closest, target, offset_start = 0, offset_end = 0;
	int measure[1000], raster[101];

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	// write IP and port into sockaddr structure
	struct sockaddr_in sock_addr_in = { 0 };
	sock_addr_in.sin_family = AF_INET;
	sock_addr_in.sin_port = htons(1975);
	sock_addr_in.sin_addr.s_addr = inet_addr(addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	CURL *curl = curl_init(URL_METER, callback_calibrate);
	if (curl == NULL)
		perror("Error initializing libcurl");

	printf("starting calibration on %s (%s)\n", name, addr);
	snprintf(message, 16, "v:0:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// average offset power at start
	printf("calculating offset start");
	for (int i = 0; i < 10; i++) {
		curl_easy_perform(curl);
		offset_start += grid;
		printf(" %d", grid);
		sleep(1);
	}
	offset_start /= 10;
	printf(" --> average %d\n", offset_start);

	printf("waiting for heat up 100%%...\n");
	snprintf(message, 16, "v:10000:0");
	sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));
	sleep(5);

	// get maximum power
	curl_easy_perform(curl);
	int max_power = round100(grid - offset_start);

	int onepercent = max_power / 100;
	printf("starting measurement with maximum power %d watt 1%%=%d watt\n", max_power, onepercent);

	// do a full drive over SSR characteristic load curve from 10 down to 0 volt and capture power
	for (int i = 0; i < 1000; i++) {
		voltage = 10000 - (i * 10);

		snprintf(message, 16, "v:%d:%d", voltage, 0);
		sendto(sock, message, strlen(message), 0, sa, sizeof(*sa));

		// give SSR time to set voltage and smart meter to measure
		if (2000 < voltage && voltage < 8000)
			usleep(1000 * 1000); // more time between 8 and 2 volts
		else
			usleep(1000 * 600);

		curl_easy_perform(curl);
		measure[i] = grid - offset_start;
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
		curl_easy_perform(curl);
		offset_end += grid;
		printf(" %d", grid);
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
	curl_easy_cleanup(curl);
	return 0;
}

static int test() {
	device_t *d = &boiler1;

	d->active = 1;
	d->power = -1;
	d->addr = resolve_ip(d->name);

	// 2 x blink
	set_boiler(d, 100);
	sleep(1);
	set_boiler(d, 0);
	sleep(1);
	set_boiler(d, 100);
	sleep(1);
	set_boiler(d, 0);
	sleep(1);

	// full ramp up 0..100
	for (int i = 0; i <= 100; i++) {
		set_boiler(d, i);
		usleep(200 * 1000);
	}
	set_boiler(d, 0);
	return 0;
}

static int init() {
	set_debug(1);

	init_all_devices();

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

	if (sock != 0)
		close(sock);
}

int fronius_override(const char *name) {
	for (int i = 0; i < ARRAY_SIZE(devices); i++) {
		device_t *d = devices[i];
		if (!strcmp(d->name, name)) {
			xlog("FRONIUS Activating Override for %d seconds on %s", OVERRIDE, d->name);
			d->override = time(NULL) + OVERRIDE;
			d->active = 1;
			d->standby = 0;
			(d->set_function)(d, 100);
		}
	}
	return 0;
}

int fronius_main(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	// no arguments - main loop
	if (argc == 1) {
		init();
		pause();
		stop();
		return 0;
	}

	init_all_devices();

	int c;
	while ((c = getopt(argc, argv, "c:o:t")) != -1) {
		switch (c) {
		case 'c':
			// execute as: stdbuf -i0 -o0 -e0 ./fronius -c boiler1 > boiler1.txt
			return calibrate(optarg);
		case 'o':
			return fronius_override(optarg);
		case 't':
			return test();
		}
	}

	return 0;
}

#ifdef FRONIUS_MAIN
int main(int argc, char **argv) {
	return fronius_main(argc, argv);
}
#else
MCP_REGISTER(fronius, 7, &init, &stop);
#endif
