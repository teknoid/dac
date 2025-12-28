#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>

#include "sunspec.h"
#include "utils.h"

// gcc -DSUNSPEC_MAIN -I ./include/ -o sunspec sunspec.c utils.c -lmodbus -lpthread -lm

// store fixed values and scaling factors internally
static int wchamax, inoutwrte_sf, minrsvpct_sf;

static void swap_string(char *string, int size) {
	uint16_t *x = (uint16_t*) string;
	uint16_t *y = (uint16_t*) string + size / 2;
	for (; x < y; x++)
		SWAP16(*x);
}

static int collect_models(sunspec_t *ss) {
	uint16_t index[2] = { 0, 0 };
	uint16_t *id = &index[0];
	uint16_t *size = &index[1];
	int rc;

	uint32_t sunspec_id = 0;
	int address = SUNSPEC_BASE_ADDRESS;
	rc = modbus_read_registers(ss->mb, address, 2, (uint16_t*) &sunspec_id);
	if (rc == -1)
		return xerr("SUNSPEC %s modbus_read_registers %s", ss->name, modbus_strerror(errno));

	// 0x53756e53 == 'SunS'
	SWAP32(sunspec_id);
	if (sunspec_id != 0x53756e53)
		return xerr("SUNSPEC %s no 'SunS' found at address %d", ss->name, address);

	xlog("SUNSPEC %s found 'SunS' at address %d", ss->name, address);
	address += 2;

	while (1) {
		pthread_mutex_lock(&ss->lock);
		rc = modbus_read_registers(ss->mb, address, 2, (uint16_t*) &index);
		pthread_mutex_unlock(&ss->lock);

		if (rc == -1)
			return xerr("SUNSPEC %s modbus_read_registers %s", ss->name, modbus_strerror(errno));

		if (*id == 0xffff && *size == 0)
			break;

		switch (*id) {

		case 001:
			ss->common_addr = address;
			ss->common_size = *size;
			ss->common_id = *id;
			if (!ss->common)
				ss->common = malloc(SUNSPEC_COMMON_SIZE);
			ZEROP(ss->common);
			xlog("SUNSPEC %s found Common(%03d) model size %d at address %d", ss->name, *id, *size, address);
			if (*size * sizeof(uint16_t) + 4 != SUNSPEC_COMMON_SIZE)
				xlog("SUNSPEC %s WARNING! Common(%03d) size mismatch detected: model size %d storage size %d", ss->name, *id, *size, SUNSPEC_COMMON_SIZE);
			break;

		case 101:
		case 102:
		case 103:
			ss->inverter_addr = address;
			ss->inverter_size = *size;
			ss->inverter_id = *id;
			if (!ss->inverter)
				ss->inverter = malloc(SUNSPEC_INVERTER_SIZE);
			ZEROP(ss->inverter);
			xlog("SUNSPEC %s found Inverter(%03d) model size %d at address %d", ss->name, *id, *size, address);
			if (*size * sizeof(uint16_t) + 4 != SUNSPEC_INVERTER_SIZE)
				xlog("SUNSPEC %s WARNING! Inverter(%03d) size mismatch detected: model size %d storage size %d", ss->name, *id, *size, SUNSPEC_INVERTER_SIZE);
			break;

		case 201:
		case 202:
		case 203:
			ss->meter_addr = address;
			ss->meter_size = *size;
			ss->meter_id = *id;
			if (!ss->meter)
				ss->meter = malloc(SUNSPEC_METER_SIZE);
			ZEROP(ss->meter);
			xlog("SUNSPEC %s found Meter(%03d) model size %d at address %d", ss->name, *id, *size, address);
			if (*size * sizeof(uint16_t) + 4 != SUNSPEC_METER_SIZE)
				xlog("SUNSPEC %s WARNING! Meter(%03d) size mismatch detected: model size %d storage size %d", ss->name, *id, *size, SUNSPEC_METER_SIZE);
			break;

		case 120:
			ss->nameplate_addr = address;
			ss->nameplate_size = *size;
			ss->nameplate_id = *id;
			if (!ss->nameplate)
				ss->nameplate = malloc(SUNSPEC_NAMEPLATE_SIZE);
			ZEROP(ss->nameplate);
			xlog("SUNSPEC %s found Nameplate(%03d) model size %d at address %d", ss->name, *id, *size, address);
			if (*size * sizeof(uint16_t) + 4 != SUNSPEC_NAMEPLATE_SIZE)
				xlog("SUNSPEC %s WARNING! Nameplate(%03d) size mismatch detected: model size %d storage size %d", ss->name, *id, *size, SUNSPEC_NAMEPLATE_SIZE);
			break;

		case 121:
			ss->settings_addr = address;
			ss->settings_size = *size;
			ss->settings_id = *id;
			if (!ss->settings)
				ss->settings = malloc(SUNSPEC_SETTINGS_SIZE);
			ZEROP(ss->settings);
			xlog("SUNSPEC %s found Settings(%03d) model size %d at address %d", ss->name, *id, *size, address);
			if (*size * sizeof(uint16_t) + 4 != SUNSPEC_SETTINGS_SIZE)
				xlog("SUNSPEC %s WARNING! Settings(%03d) size mismatch detected: model size %d storage size %d", ss->name, *id, *size, SUNSPEC_SETTINGS_SIZE);
			break;

		case 122:
			ss->status_addr = address;
			ss->status_size = *size;
			ss->status_id = *id;
			if (!ss->status)
				ss->status = malloc(SUNSPEC_STATUS_SIZE);
			ZEROP(ss->status);
			xlog("SUNSPEC %s found Status(%03d) model size %d at address %d", ss->name, *id, *size, address);
			if (*size * sizeof(uint16_t) + 4 != SUNSPEC_STATUS_SIZE)
				xlog("SUNSPEC %s WARNING! Status(%03d) size mismatch detected: model size %d storage size %d", ss->name, *id, *size, SUNSPEC_STATUS_SIZE);
			break;

		case 123:
			ss->controls_addr = address;
			ss->controls_size = *size;
			ss->controls_id = *id;
			if (!ss->controls)
				ss->controls = malloc(SUNSPEC_CONTROLS_SIZE);
			ZEROP(ss->controls);
			xlog("SUNSPEC %s found Controls(%03d) model size %d at address %d", ss->name, *id, *size, address);
			if (*size * sizeof(uint16_t) + 4 != SUNSPEC_CONTROLS_SIZE)
				xlog("SUNSPEC %s WARNING! Controls(%03d) size mismatch detected: model size %d storage size %d", ss->name, *id, *size, SUNSPEC_CONTROLS_SIZE);
			break;

		case 124:
			ss->storage_addr = address;
			ss->storage_size = *size;
			ss->storage_id = *id;
			if (!ss->storage)
				ss->storage = malloc(SUNSPEC_STORAGE_SIZE);
			ZEROP(ss->storage);
			xlog("SUNSPEC %s found Storage(%03d) model size %d at address %d", ss->name, *id, *size, address);
			if (*size * sizeof(uint16_t) + 4 != SUNSPEC_STORAGE_SIZE)
				xlog("SUNSPEC %s WARNING! Storage(%03d) size mismatch detected: model size %d storage size %d", ss->name, *id, *size, SUNSPEC_STORAGE_SIZE);
			break;

		case 160:
			ss->mppt_addr = address;
			ss->mppt_size = *size;
			ss->mppt_id = *id;
			if (!ss->mppt)
				ss->mppt = malloc(SUNSPEC_MPPT_SIZE);
			ZEROP(ss->mppt);
			xlog("SUNSPEC %s found MPPT(%03d) model size %d at address %d", ss->name, *id, *size, address);
			if (*size * sizeof(uint16_t) + 4 != SUNSPEC_MPPT_SIZE)
				xlog("SUNSPEC %s WARNING! MPPT(%03d) size mismatch detected: model size %d storage size %d", ss->name, *id, *size, SUNSPEC_MPPT_SIZE);
			break;

		default:
			xlog("SUNSPEC %s unknown model %d size %d at address %d", ss->name, *id, *size, address);
		}
		address += *size + 2;
	}

	return 0;
}

static int read_model(sunspec_t *ss, uint16_t id, uint16_t addr, uint16_t size, uint16_t *model) {
	// xdebug("SUNSPEC %s read_model %d size %d", ss->name, id, size);

	pthread_mutex_lock(&ss->lock);
	int rc = modbus_read_registers(ss->mb, addr, size + 2, model);
	pthread_mutex_unlock(&ss->lock);

	if (rc == -1) {
		memset(model, 0, size * sizeof(uint16_t) + 4);
		return xerr("SUNSPEC %s modbus_read_registers %s", ss->name, modbus_strerror(errno));
	}

	// validate id + size
	uint16_t *model_id = model;
	uint16_t *model_size = model + 1;
	if (*model_id != id || *model_size != size)
		return xerr("SUNSPEC %s model validation failed (ID=%d L=%d)", ss->name, *model_id, *model_size);

	return 0;
}

// TODO more int32 mappings

static int read_common(sunspec_t *ss) {
	if (!ss->common)
		return 0;

	int rc = read_model(ss, ss->common_id, ss->common_addr, ss->common_size, (uint16_t*) ss->common);
	swap_string(ss->common->Mn, 32);
	swap_string(ss->common->Md, 32);
	swap_string(ss->common->Opt, 16);
	swap_string(ss->common->Vr, 16);
	swap_string(ss->common->SN, 32);
	xlog("SUNSPEC %s device is %s %s (%s) version %s serial %s", ss->name, ss->common->Mn, ss->common->Md, ss->common->Opt, ss->common->Vr, ss->common->SN);
	return rc;
}

static int read_nameplate(sunspec_t *ss) {
	if (!ss->nameplate)
		return 0;

	int rc = read_model(ss, ss->nameplate_id, ss->nameplate_addr, ss->nameplate_size, (uint16_t*) ss->nameplate);
	return rc;
}

static int read_settings(sunspec_t *ss) {
	if (!ss->settings)
		return 0;

	int rc = read_model(ss, ss->settings_id, ss->settings_addr, ss->settings_size, (uint16_t*) ss->settings);
	return rc;
}

static int read_status(sunspec_t *ss) {
	if (!ss->status)
		return 0;

	int rc = read_model(ss, ss->status_id, ss->status_addr, ss->status_size, (uint16_t*) ss->status);
	swap_string(ss->status->TmSrc, 8);
	return rc;
}

static int read_controls(sunspec_t *ss) {
	if (!ss->controls)
		return 0;

	int rc = read_model(ss, ss->controls_id, ss->controls_addr, ss->controls_size, (uint16_t*) ss->controls);
	return rc;
}

static int read_inverter(sunspec_t *ss) {
	if (!ss->inverter)
		return 0;

	int rc = read_model(ss, ss->inverter_id, ss->inverter_addr, ss->inverter_size, (uint16_t*) ss->inverter);
	return rc;
}

static int read_mppt(sunspec_t *ss) {
	if (!ss->mppt)
		return 0;

	int rc = read_model(ss, ss->mppt_id, ss->mppt_addr, ss->mppt_size, (uint16_t*) ss->mppt);
	swap_string(ss->mppt->m1_IDStr, 16);
	swap_string(ss->mppt->m2_IDStr, 16);
	SWAP32(ss->mppt->m1_DCWH);
	SWAP32(ss->mppt->m2_DCWH);
	return rc;
}

static int read_storage(sunspec_t *ss) {
	if (!ss->storage)
		return 0;

	int rc = read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);
	wchamax = SFI(ss->storage->WChaMax, ss->storage->WChaMax_SF);
	inoutwrte_sf = ss->storage->InOutWRte_SF;
	minrsvpct_sf = ss->storage->MinRsvPct_SF;
	return rc;
}

static int read_meter(sunspec_t *ss) {
	if (!ss->meter)
		return 0;

	int rc = read_model(ss, ss->meter_id, ss->meter_addr, ss->meter_size, (uint16_t*) ss->meter);
	SWAP32(ss->meter->TotWhExp);
	SWAP32(ss->meter->TotWhImp);
	return rc;
}

static void* poll(void *arg) {
	int rc, errors_all, errors_now;
	sunspec_t *ss = (sunspec_t*) arg;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL))
		return (void*) 0;

	while (1) {
		errors_all = 0;

		ss->mb = modbus_new_tcp(ss->ip, 502);
		modbus_set_response_timeout(ss->mb, 5, 0);
		modbus_set_error_recovery(ss->mb, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

		rc = modbus_set_slave(ss->mb, ss->slave);
		if (rc == -1)
			xlog("SUNSPEC %s invalid modbus slave id %d", ss->name, ss->slave);

		if (modbus_connect(ss->mb) == -1) {
			xlog("SUNSPEC %s connection failed: %s, retry in %d seconds", ss->ip, modbus_strerror(errno), CONNECT_RETRY_TIME);
			modbus_free(ss->mb);
			ss->mb = 0;
			sleep(CONNECT_RETRY_TIME);
			continue;
		}

		if (collect_models(ss) == -1) {
			xlog("SUNSPEC %s collect_models() error: %s, retry in %d seconds", ss->ip, modbus_strerror(errno), CONNECT_RETRY_TIME);
			modbus_free(ss->mb);
			ss->mb = 0;
			sleep(CONNECT_RETRY_TIME);
			continue;
		}

		// read static models once
		errors_all += read_common(ss);
		errors_all += read_nameplate(ss);
		errors_all += read_settings(ss);
		errors_all += read_status(ss);
		errors_all += read_controls(ss);
		errors_all += read_storage(ss);

		while (errors_all > -10) {

			// PROFILING_START

			// read dynamic models in the loop
			errors_now = 0;
			errors_now += read_inverter(ss);
			errors_now += read_mppt(ss);
			errors_now += read_meter(ss);
			// storage once per minute
			if (ss->ts % 60 == 0)
				errors_now += read_storage(ss);
			errors_all += errors_now;

			// PROFILING_LOG(ss->name)

			// xdebug("SUNSPEC %s meter grid %d", ss->name, ss->meter->W);
			// xdebug("SUNSPEC %s poll time %d", ss->name, ss->poll_time_ms);

			// update time stamp
			ss->ts = time(NULL);

			// execute the callback function to process model data - only if error-free
			if (ss->callback && errors_now == 0)
				(ss->callback)(ss);

			// wait for new second
			while (ss->ts == time(NULL))
				msleep(111);

			// middle of second
			msleep(555);

			// pause when set
			if (ss->sleep)
				sleep(ss->sleep);
		}

		xlog("SUNSPEC %s aborting poll after too many errors", ss->name);
		if (ss->inverter)
			ss->inverter->St = 0;

		modbus_close(ss->mb);
		modbus_free(ss->mb);
		ss->mb = 0;
	}

	return (void*) 0;
}

void sunspec_write_reg(sunspec_t *ss, int addr, const uint16_t value) {
	pthread_mutex_lock(&ss->lock);
	int rc = modbus_write_register(ss->mb, addr, value);
	pthread_mutex_unlock(&ss->lock);
	if (rc == -1)
		xerr("SUNSPEC %s modbus_write_register %s", ss->name, modbus_strerror(errno));
}

void sunspec_read_reg(sunspec_t *ss, int addr, uint16_t *value) {
	pthread_mutex_lock(&ss->lock);
	int rc = modbus_read_registers(ss->mb, addr, 1, value);
	pthread_mutex_unlock(&ss->lock);
	if (rc == -1) {
		*value = 0;
		xerr("SUNSPEC %s modbus_read_registers %s", ss->name, modbus_strerror(errno));
	}
}

int sunspec_read(sunspec_t *ss) {
	int errors = 0;
	errors += read_common(ss);
	errors += read_nameplate(ss);
	errors += read_settings(ss);
	errors += read_status(ss);
	errors += read_controls(ss);
	errors += read_inverter(ss);
	errors += read_mppt(ss);
	errors += read_storage(ss);
	errors += read_meter(ss);
	return errors;
}

sunspec_t* sunspec_init(const char *name, int slave) {
	sunspec_t *ss = malloc(sizeof(sunspec_t));
	ZEROP(ss);

	ss->name = name;
	ss->slave = slave;
	ss->control = 1;
	ss->sleep = 0;

	ss->ip = resolve_ip(ss->name);
	if (!ss->ip)
		return 0;

	pthread_mutex_init(&ss->lock, NULL);

	ss->mb = modbus_new_tcp(ss->ip, 502);
	modbus_set_response_timeout(ss->mb, 5, 0);
	modbus_set_error_recovery(ss->mb, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

	int rc = modbus_set_slave(ss->mb, ss->slave);
	if (rc == -1)
		xlog("SUNSPEC %s invalid modbus slave id %d", ss->name, ss->slave);

	if (modbus_connect(ss->mb) == -1) {
		xlog("SUNSPEC connection to %s failed: %s, retry in %d seconds", ss->ip, modbus_strerror(errno), CONNECT_RETRY_TIME);
		modbus_free(ss->mb);
		return 0;
	}

	collect_models(ss);
	return ss;
}

sunspec_t* sunspec_init_poll(const char *name, int slave, const sunspec_callback_t callback) {
	sunspec_t *ss = malloc(sizeof(sunspec_t));
	ZEROP(ss);

	ss->name = name;
	ss->slave = slave;
	ss->control = 1;
	ss->sleep = 0;
	ss->callback = callback;

	ss->ip = resolve_ip(ss->name);
	if (!ss->ip)
		return 0;

	pthread_mutex_init(&ss->lock, NULL);

	if (pthread_create(&ss->thread, NULL, &poll, ss))
		xerr("SUNSPEC Error creating %s poll thread", ss->name);

	xlog("SUNSPEC initialized %s", ss->name);
	return ss;
}

void sunspec_stop(sunspec_t *ss) {
	if (!ss)
		return;

	if (ss->thread)
		if (pthread_cancel(ss->thread))
			xerr("SUNSPEC Error canceling %s poll thread", ss->name);

	if (ss->thread)
		if (pthread_join(ss->thread, NULL))
			xerr("SUNSPEC Error joining %s poll thread", ss->name);

	if (ss->mb)
		modbus_close(ss->mb);

	if (ss->mb)
		modbus_free(ss->mb);

	pthread_mutex_destroy(&ss->lock);

	xlog("SUNSPEC stopped %s", ss->name);
	free(ss);
}

int sunspec_storage_limit_both(sunspec_t *ss, int in, int out) {
	if (!ss->control)
		return EPERM;

	if (!ss->storage)
		return ENOENT;

	if (!wchamax)
		return ENOENT;

	if (in < 0)
		in = 0;
	if (in > wchamax)
		in = wchamax;

	if (out < 0)
		out = 0;
	if (out > wchamax)
		out = wchamax;

	int inwrte = SFOUT(in, inoutwrte_sf) * 100 / wchamax;
	int outwrte = SFOUT(out, inoutwrte_sf) * 100 / wchamax;

	if (ss->storage->StorCtl_Mod == STORAGE_LIMIT_BOTH && ss->storage->InWRte == inwrte && ss->storage->OutWRte == outwrte)
		return EALREADY; // already set

	xlog("SUNSPEC set charge limit to %d, W discharge limit to %d W", in, out);
	sunspec_write_reg(ss, ss->storage_addr + OFFSET(ss->storage, ss->storage->StorCtl_Mod), STORAGE_LIMIT_BOTH);
	sunspec_write_reg(ss, ss->storage_addr + OFFSET(ss->storage, ss->storage->InWRte), inwrte);
	sunspec_write_reg(ss, ss->storage_addr + OFFSET(ss->storage, ss->storage->OutWRte), outwrte);
	return read_storage(ss);
}

int sunspec_storage_limit_charge(sunspec_t *ss, int in) {
	if (!ss->control)
		return EPERM;

	if (!ss->storage)
		return ENOENT;

	if (!wchamax)
		return ENOENT;

	if (in < 0)
		in = 0;
	if (in > wchamax)
		in = wchamax;

	int inwrte = SFOUT(in, inoutwrte_sf) * 100 / wchamax;
	if (ss->storage->StorCtl_Mod == STORAGE_LIMIT_CHARGE && ss->storage->InWRte == inwrte)
		return EALREADY; // already set

	xlog("SUNSPEC set charge limit to %d W", in);
	sunspec_write_reg(ss, ss->storage_addr + OFFSET(ss->storage, ss->storage->StorCtl_Mod), STORAGE_LIMIT_CHARGE);
	sunspec_write_reg(ss, ss->storage_addr + OFFSET(ss->storage, ss->storage->InWRte), inwrte);
	return read_storage(ss);
}

int sunspec_storage_limit_discharge(sunspec_t *ss, int out) {
	if (!ss->control)
		return EPERM;

	if (!ss->storage)
		return ENOENT;

	if (!wchamax)
		return ENOENT;

	if (out < 0)
		out = 0;
	if (out > wchamax)
		out = wchamax;

	int outwrte = SFOUT(out, inoutwrte_sf) * 100 / wchamax;
	if (ss->storage->StorCtl_Mod == STORAGE_LIMIT_DISCHARGE && ss->storage->OutWRte == outwrte)
		return EALREADY; // already set

	xlog("SUNSPEC set discharge limit to %d W", out);
	sunspec_write_reg(ss, ss->storage_addr + OFFSET(ss->storage, ss->storage->StorCtl_Mod), STORAGE_LIMIT_DISCHARGE);
	sunspec_write_reg(ss, ss->storage_addr + OFFSET(ss->storage, ss->storage->OutWRte), outwrte);
	return read_storage(ss);
}

int sunspec_storage_limit_reset(sunspec_t *ss) {
	if (!ss->control)
		return EPERM;

	if (!ss->storage)
		return ENOENT;

	if (ss->storage->StorCtl_Mod == STORAGE_LIMIT_NONE)
		return EALREADY; // already set

	int inoutwrte = SFOUT(100, inoutwrte_sf);
	xlog("SUNSPEC reset charge/discharge limits");
	sunspec_write_reg(ss, ss->storage_addr + OFFSET(ss->storage, ss->storage->StorCtl_Mod), STORAGE_LIMIT_NONE);
	sunspec_write_reg(ss, ss->storage_addr + OFFSET(ss->storage, ss->storage->InWRte), inoutwrte);
	sunspec_write_reg(ss, ss->storage_addr + OFFSET(ss->storage, ss->storage->OutWRte), inoutwrte);
	return read_storage(ss);
}

int sunspec_storage_minimum_soc(sunspec_t *ss, int soc) {
	if (!ss->control)
		return EPERM;

	if (!ss->storage)
		return ENOENT;

	int minrsvpct = SFOUT(soc, minrsvpct_sf);
	if (ss->storage->MinRsvPct == minrsvpct)
		return EALREADY; // already set

	xlog("SUNSPEC setting minimum SoC to %d%%", soc);
	sunspec_write_reg(ss, ss->storage_addr + OFFSET(ss->storage, ss->storage->MinRsvPct), minrsvpct);
	return read_storage(ss);
}

// inverter: ./sunspec -s -h 192.168.25.240 -p 502 -a 40000
// meter:    ./sunspec -s -h 192.168.25.240 -p 502 -a 40000 -v 200
static int scan(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	char *host;
	int c, port = 502, addr = 0, slave = 1;
	while ((c = getopt(argc, argv, "a:h:p:v:")) != -1) {
		switch (c) {
		case 'a':
			addr = atoi(optarg);
			break;
		case 'h':
			host = optarg;
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'v':
			slave = atoi(optarg);
			break;
		default:
			xlog("unknown getopt %c", c);
		}
	}

	modbus_t *mb = modbus_new_tcp(host, port);
	modbus_set_response_timeout(mb, 5, 0);
	modbus_set_error_recovery(mb, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);
	modbus_set_slave(mb, slave);
	int rc = modbus_connect(mb);
	if (rc != 0)
		return xerrr(rc, "SUNSPEC modbus_connect returned %d", rc);

	uint16_t *buffer = malloc(sizeof(uint16_t) * MODBUS_MAX_READ_REGISTERS);
	modbus_read_registers(mb, addr, MODBUS_MAX_READ_REGISTERS, buffer);
	uint16_t *p = buffer;
	for (int i = 0; i < MODBUS_MAX_READ_REGISTERS; i++)
		xlog("%5d = %d", addr + i, *p++);

	modbus_close(mb);
	modbus_free(mb);
	return 0;
}
static int test1(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

	sunspec_t *ss = sunspec_init("fronius10", 1);
	sunspec_read(ss);
	sunspec_stop(ss);
	return 0;
}

static int test2(int argc, char **argv) {
	set_xlog(XLOG_STDOUT);
	set_debug(1);

// 16bit:38 + 32bit:7
#define INVERTER_SIZE		52
#define INVERTER_OFFSET		40070

// 16bit:20 + 32bit:7 + char[16]:2 = 50
#define MPPT_SIZE10			90
#define MPPT_SIZE7			50
#define MPPT_OFFSET			40254

	// 16bit:26 = 26
#define STORAGE_SIZE		26
#define STORAGE_OFFSET		40344

	// 16bit:41 + 32bit:33 = 107
#define METER_SIZE			107
#define METER_OFFSET		40070

	modbus_t *mb = modbus_new_tcp("192.168.25.240", 502);
	modbus_set_response_timeout(mb, 5, 0);
	modbus_set_error_recovery(mb, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);
	modbus_set_slave(mb, 1);
	// modbus_set_slave(mb, 2);
	int rc = modbus_connect(mb);
	if (rc != 0)
		return xerrr(rc, "SUNSPEC modbus_connect returned %d", rc);

	uint32_t sunspec_id = 0;
	int address = SUNSPEC_BASE_ADDRESS;
	modbus_read_registers(mb, address, 2, (uint16_t*) &sunspec_id);

	// 0x53756e53 == 'SunS'
	SWAP32(sunspec_id);
	if (sunspec_id != 0x53756e53)
		return xerr("SUNSPEC no 'SunS' found at address %d", address);

	xlog("SUNSPEC found 'SunS' at address %d", address);
	address += 2;

	uint16_t index[2] = { 0, 0 };
	while (1) {
		modbus_read_registers(mb, address, 2, (uint16_t*) &index);
		if (index[0] == 0xffff && index[1] == 0)
			break;
		xlog("ID %d Size %d Offset %d", index[0], index[1], address);
		address += index[1] + 2;
	}

	sunspec_inverter_t inverter;
	sunspec_meter_t meter;
	sunspec_mppt_t mppt;

	while (1) {
		ZERO(inverter);
		ZERO(mppt);
		ZERO(meter);

		modbus_read_registers(mb, 40069, 50, (uint16_t*) &inverter);
		modbus_read_registers(mb, 40253, 48, (uint16_t*) &mppt);
		// modbus_set_slave(mb, 200);
		// modbus_read_registers(mb, METER_OFFSET - 1, METER_SIZE, (uint16_t*) &meter);

		xlog("inverter model validation ID=%d L=%d", inverter.ID, inverter.L);
		xlog("mppt     model validation ID=%d L=%d", mppt.ID, mppt.L);

		xlog("Status %d", inverter.St);
		xlog("raw  mppt DCW1:%u DCW2:%u", mppt.m1_DCW, mppt.m2_DCW);
		xlog("raw  mppt Tms1:%u DCWH1:%12u Tms2:%12u DCWH2:%12u", mppt.m1_Tms, mppt.m1_DCWH, mppt.m2_Tms, mppt.m2_DCWH);
		xlog("SWAP mppt Tms1:%u DCWH1:%12u Tms2:%12u DCWH2:%12u", SWAP32(mppt.m1_Tms), SWAP32(mppt.m1_DCWH), SWAP32(mppt.m2_Tms), SWAP32(mppt.m2_DCWH));

		sleep(3);
	}

	modbus_close(mb);
	modbus_free(mb);
}

int sunspec_main(int argc, char **argv) {
	int c;
	while ((c = getopt(argc, argv, "st")) != -1) {
		// printf("getopt %c\n", c);
		switch (c) {
		case 't':
			test1(argc, argv);
			test2(argc, argv);
			return 0;
		case 's':
			scan(argc, argv);
			return 0;
		default:
			xlog("unknown getopt %c", c);
		}
	}

	return 0;
}

#ifdef SUNSPEC_MAIN
int main(int argc, char **argv) {
	return sunspec_main(argc, argv);
}
#endif
