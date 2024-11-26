#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include "sunspec.h"
#include "utils.h"

// gcc -DSUNSPEC_MAIN -I ./include/ -o sunspec sunspec.c utils.c -lmodbus -lpthread

static void collect_models(sunspec_t *ss) {
	uint16_t index[2] = { 0, 0 };
	uint16_t *id = &index[0];
	uint16_t *size = &index[1];

	int address = SUNSPEC_BASE_ADDRESS - 1;
	while (1) {
		modbus_read_registers(ss->mb, address, 2, (uint16_t*) &index);

		if (*id == 0xffff && *size == 0)
			break;

		switch (*id) {

		case 101:
		case 102:
		case 103:
			ss->inverter_addr = address;
			ss->inverter_size = *size;
			ss->inverter_id = *id;
			if (!ss->inverter)
				ss->inverter = malloc(sizeof(sunspec_inverter_t));
			ZEROP(ss->inverter);
			break;

		case 201:
		case 202:
		case 203:
			ss->meter_addr = address;
			ss->meter_size = *size;
			ss->meter_id = *id;
			if (!ss->meter)
				ss->meter = malloc(sizeof(sunspec_meter_t));
			ZEROP(ss->meter);
			break;

		case 124:
			ss->storage_addr = address;
			ss->storage_size = *size;
			ss->storage_id = *id;
			if (!ss->storage)
				ss->storage = malloc(sizeof(sunspec_storage_t));
			ZEROP(ss->storage);
			break;

		case 160:
			ss->mppt_addr = address;
			ss->mppt_size = *size;
			ss->mppt_id = *id;
			if (!ss->mppt)
				ss->mppt = malloc(sizeof(sunspec_mppt_t));
			ZEROP(ss->mppt);
			break;
		}

		xlog("SUNSPEC %s found model %d size %d at address %d", ss->name, *id, *size, address);
		address += *size + 2;
	}
}

static int read_model(sunspec_t *ss, uint16_t id, uint16_t addr, uint16_t size, uint16_t *model) {
	// xdebug("SUNSPEC %s read_model %d", i->name, id);

	// zero model
	memset(model, 0, size * sizeof(uint16_t) + 2);

	// read
	int rc = modbus_read_registers(ss->mb, addr, size, model);
	if (rc == -1)
		return xerr("SUNSPEC %s modbus_read_registers %s", ss->name, modbus_strerror(errno));

	// validate id + size
	uint16_t *model_id = model;
	uint16_t *model_size = model + 1;
	if (*model_id != id || *model_size != size)
		return xerr("SUNSPEC %s model validation failed (ID=%d L=%d)", ss->name, *model_id, *model_size);

	return 0;
}

static void* poll(void *arg) {
	int rc, errors;
	sunspec_t *ss = (sunspec_t*) arg;

	if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL))
		return (void*) 0;

	while (1) {
		errors = 0;

		ss->mb = modbus_new_tcp(ss->ip, 502);
		modbus_set_response_timeout(ss->mb, 5, 0);
		modbus_set_error_recovery(ss->mb, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);

		rc = modbus_set_slave(ss->mb, ss->slave);
		if (rc == -1)
			xlog("SUNSPEC %s invalid modbus slave id %d", ss->name, ss->slave);

		if (modbus_connect(ss->mb) == -1) {
			xlog("SUNSPEC connection to %s failed: %s, retry in %d seconds", ss->ip, modbus_strerror(errno), CONNECT_RETRY_TIME);
			modbus_free(ss->mb);
			sleep(CONNECT_RETRY_TIME);
			continue;
		}

		collect_models(ss);

		// TODO read static models once here:
		// common
		// nameplate
		// basic
		// extended
		// immediate

		// read dynamic models in a loop
		while (errors > -10) {
			msleep(ss->poll);

			if (ss->inverter)
				errors += read_model(ss, ss->inverter_id, ss->inverter_addr, ss->inverter_size, (uint16_t*) ss->inverter);

			if (ss->mppt)
				errors += read_model(ss, ss->mppt_id, ss->mppt_addr, ss->mppt_size, (uint16_t*) ss->mppt);

			if (ss->storage)
				errors += read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);

			if (ss->meter)
				errors += read_model(ss, ss->meter_id, ss->meter_addr, ss->meter_size, (uint16_t*) ss->meter);

			// execute the callback function to process model data
			if (ss->callback)
				(ss->callback)(ss);

//			xdebug("SUNSPEC %s meter grid %d", ss->name, ss->meter->W);
//			xdebug("SUNSPEC %s poll time %d", ss->name, ss->poll_time_ms);
		}

		xlog("SUNSPEC aborting %s poll due to too many errors: %d", ss->name, abs(errors));

		modbus_close(ss->mb);
		modbus_free(ss->mb);
	}

	return (void*) 0;
}

sunspec_t* sunspec_init(const char *name, const char *ip, int slave, const sunspec_callback_t callback) {
	sunspec_t *ss = malloc(sizeof(sunspec_t));
	ZEROP(ss);

	ss->ip = ip;
	ss->name = name;
	ss->slave = slave;
	ss->callback = callback;
	ss->poll = POLL_TIME_ACTIVE;

	if (pthread_create(&ss->thread, NULL, &poll, ss))
		xerr("SUNSPEC Error creating %s poll thread", ss->name);

	xlog("SUNSPEC initialized %s", ss->name);
	return ss;
}

void sunspec_stop(sunspec_t *ss) {
	if (!ss)
		return;

	if (pthread_cancel(ss->thread))
		xerr("SUNSPEC Error canceling %s poll thread", ss->name);

	if (pthread_join(ss->thread, NULL))
		xerr("SUNSPEC Error joining %s poll thread", ss->name);

	xlog("SUNSPEC stopped %s", ss->name);
	free(ss);
}

int test(int argc, char **argv) {
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

	modbus_t *mb = modbus_new_tcp("192.168.25.231", 502);
	modbus_set_response_timeout(mb, 5, 0);
	modbus_set_error_recovery(mb, MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL);
	modbus_set_slave(mb, 2);
	modbus_connect(mb);

	uint16_t index[2] = { 0, 0 };
	int offset = SUNSPEC_BASE_ADDRESS - 1;
	while (1) {
		modbus_read_registers(mb, offset, 2, (uint16_t*) &index);
		if (index[0] == 0xffff && index[1] == 0)
			break;
		xlog("ID %d Size %d Offset %d", index[0], index[1], offset);
		offset += index[1] + 2;
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
//		modbus_set_slave(mb, 200);
//		modbus_read_registers(mb, METER_OFFSET - 1, METER_SIZE, (uint16_t*) &meter);

		xlog("inverter model validation ID=%d L=%d", inverter.ID, inverter.L);
		xlog("mppt     model validation ID=%d L=%d", mppt.ID, mppt.L);

		xlog("Status %d", inverter.St);
		xlog("raw  mppt DCW1:%u DCW2:%u", mppt.DCW1, mppt.DCW2);
		xlog("raw  mppt Tms1:%u DCWH1:%u Tms2:%u DCWH2:%u", mppt.Tms1, mppt.DCWH1, mppt.Tms2, mppt.DCWH2);
		xlog("SWAP mppt Tms1:%u DCWH1:%u Tms2:%u DCWH2:%u", SWAP32(mppt.Tms1), SWAP32(mppt.DCWH1), SWAP32(mppt.Tms2), SWAP32(mppt.DCWH2));

		sleep(3);
	}

	modbus_close(mb);
	modbus_free(mb);
}

#ifdef SUNSPEC_MAIN
int main(int argc, char **argv) {
	return test(argc, argv);
}
#endif
