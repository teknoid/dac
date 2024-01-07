#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <curl/curl.h>

#include "fronius.h"
#include "frozen.h"
#include "utils.h"
#include "mcp.h"

static pthread_t thread;
static int wait = 3;
static int standby_timer;
static get_request_t req = { .buffer = NULL, .len = 0, .buflen = CHUNK_SIZE };
static CURL *curl;

// boiler status
const char *boilers[] = { BOILERS };
static boiler_t **boiler;

// UDP socket communication
static int sock;
static struct sockaddr_in sock_addr_in = { .sin_family = AF_INET, .sin_port = 1975 };

static size_t callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
	size_t realsize = size * nmemb;
	get_request_t *req = (get_request_t*) userdata;

	if (req->buffer == NULL)
		req->buffer = malloc(CHUNK_SIZE);

	while (req->buflen < req->len + realsize + 1) {
		req->buffer = realloc(req->buffer, req->buflen + CHUNK_SIZE);
		req->buflen += CHUNK_SIZE;
	}

	memcpy(&req->buffer[req->len], ptr, realsize);
	req->len += realsize;
	req->buffer[req->len] = 0;

	return realsize;
}

static void print_status() {
	printf("boiler");
	for (int i = 0; i < ARRAY_SIZE(boilers); i++)
		printf(" %3d", boiler[i]->load);

	printf("   wait %d\n", wait);
}

static int check_all(int value) {
	int check = 1;
	for (int i = 0; i < ARRAY_SIZE(boilers); i++)
		if (boiler[i]->load != value)
			check = 0;
	return check;
}

static int calculate_step(int grid) {
	// 100% == 2000 watt --> 1% == 20W
	int step = abs(grid) / 20;
	if (-200 > grid && grid < 200)
		step /= 2; // smaller steps as it's not linear
	if (!step)
		step = 1; // at least 1
	return step;
}

static void set_boiler(boiler_t *boiler) {
	char command[128], message[16];

	if (boiler->addr == NULL)
		return;

	// convert 0..100% to 2..10V SSR control voltage
	unsigned int voltage = boiler->load == 0 ? 0 : boiler->load * 80 + 2000;

	snprintf(command, 128, "curl --silent --output /dev/null -X POST http://%s/0/%d", boiler->name, voltage);
	system(command);

	// create a socket if not yet done
	if (sock == 0)
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock == 0) {
		xlog("Error creating socket");
		return;
	}

	// update IP and port in sockaddr structure
	sock_addr_in.sin_addr.s_addr = inet_addr(boiler->addr);
	struct sockaddr *sa = (struct sockaddr*) &sock_addr_in;

	// send message to boiler
	snprintf(message, 16, "%d:%d", voltage, 0);
	if (sendto(sock, message, strlen(message), 0, sa, sizeof(*sa)) < 0)
		xlog("Sendto failed");
}

static void keep() {
	wait = WAIT_KEEP;
	printf("keep\n");
}

static void offline() {
	wait = WAIT_OFFLINE;

	if (check_all(0)) {
		printf("offline\n");
		return;
	}

	printf("entering offline\n");
	for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
		boiler[i]->load = 0;
		set_boiler(boiler[i]);
	}
}

static void rampup(int grid, int load) {
	// check if all boilers are in standby mode
	if (check_all(BOILER_STANDBY)) {
		if (--standby_timer == 0) {
			// exit standby once per hour and calculate new
			for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
				boiler[i]->load = 0;
				set_boiler(boiler[i]);
			}
			wait = WAIT_KEEP;
			printf("exiting standby\n");
			return;
		}

		wait = WAIT_STANDBY;
		printf("rampup standby %d\n", standby_timer);
		return;
	}

	// check if all boilers are ramped up to 100% but do not consume power
	if (check_all(100) && (load > -1000)) {
		for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
			boiler[i]->load = BOILER_STANDBY;
			set_boiler(boiler[i]);
		}
		wait = WAIT_STANDBY;
		standby_timer = STANDBY_EXPIRE;
		printf("entering standby\n");
		return;
	}

	int step = calculate_step(grid);
	printf("rampup surplus:%d step:%d\n", abs(grid), step);
	wait = WAIT_RAMPUP;

	// rampup each boiler separately
	for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
		if (boiler[i]->load != 100) {
			boiler[i]->load += step;
			if (boiler[i]->load == BOILER_STANDBY)
				boiler[i]->load++; // not the standby value
			if (boiler[i]->load > 100)
				boiler[i]->load = 100;
			set_boiler(boiler[i]);
			return;
		}
	}
}

static void rampdown(int grid, int load) {
	// check if all boilers are ramped down
	if (check_all(0)) {
		wait = WAIT_STANDBY;
		printf("rampdown standby\n");
		return;
	}

	int step = calculate_step(grid);
	printf("rampdown overload:%d step:%d\n", abs(grid), step);
	wait = WAIT_RAMPDOWN;

	// lowering all boilers
	for (int i = ARRAY_SIZE(boilers) - 1; i >= 0; i--) {
		if (boiler[i]->load) {
			boiler[i]->load -= step;
			if (boiler[i]->load == BOILER_STANDBY)
				boiler[i]->load--; // not the standby value
			if (boiler[i]->load < 0)
				boiler[i]->load = 0;
			set_boiler(boiler[i]);
		}
	}
}

static void* fronius(void *arg) {
	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)) {
		xlog("Error setting pthread_setcancelstate");
		return (void*) 0;
	}

	// initialize all boilers
	for (int i = 0; i < ARRAY_SIZE(boilers); i++)
		set_boiler(boiler[i]);

	float p_akku, p_grid, p_load, p_pv;
	int akku, grid, load, pv;

	while (1) {
		sleep(wait);

		req.len = 0;
		CURLcode res = curl_easy_perform(curl);
		if (res != CURLE_OK) {
			xerr("No response from Fronius API");
			offline();
			continue;
		}

		// printf("Received data:/n%s\n", req.buffer);
		// json_scanf(req.buffer, req.len, "{ Body { Data { PowerReal_P_Sum:%f } } }", &grid_power);
		// printf("Grid Power %f\n", grid_power);

		json_scanf(req.buffer, req.len, "{ Body { Data { Site { P_Akku:%f, P_Grid:%f, P_Load:%f, P_PV:%f } } } }", &p_akku, &p_grid, &p_load, &p_pv);
		akku = p_akku;
		grid = p_grid;
		load = p_load;
		pv = p_pv;
		printf("Akku:%d, Grid:%d, Load:%d, PV:%d\n", akku, grid, load, pv);

		if (pv == 0)
			// no PV production, go into offline mode
			offline();
		else if (grid < -100)
			// uploading grid power over 100 watts: ramp up
			rampup(grid, load);
		else if (-100 <= grid && grid <= 0)
			// uploading grid power between 0 and 100 watts: keep current state
			keep();
		else if (grid > 0)
			// consuming grid power: ramp down
			rampdown(grid, load);

		print_status();
	}
}

static int init() {
	boiler = malloc(ARRAY_SIZE(boilers));
	for (int i = 0; i < ARRAY_SIZE(boilers); i++) {
		boiler_t *b = malloc(sizeof(boiler_t));
		b->name = boilers[i];
		b->addr = resolve_ip(b->name);
		b->load = 0;
		boiler[i] = b;
	}

	curl = curl_easy_init();
	if (curl == NULL)
		return xerr("Error initializing libcurl");

	curl_easy_setopt(curl, CURLOPT_URL, URL);
	curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "http");
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void* ) &req);

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
	free(req.buffer);
	curl_easy_cleanup(curl);

	if (sock != 0)
		close(sock);
}

int fronius_main(int argc, char **argv) {
	init();

	pause();

	stop();
	return 0;
}

#ifdef FRONIUS_MAIN
int main(int argc, char **argv) {
	return fronius_main(argc, argv);
}
#else
MCP_REGISTER(fronius, 1, &init, &stop);
#endif

