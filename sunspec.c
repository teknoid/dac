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

// gcc -DSUNSPEC_MAIN -I ./include/ -o sunspec sunspec.c utils.c -lmodbus -lpthread

static void swap_string(char *string, int size) {
	uint16_t *x = (uint16_t*) string;
	uint16_t *y = (uint16_t*) string + size / 2;
	for (; x < y; x++)
		SWAP16(*x);
}

static void map_common(sunspec_common_t *m) {
	swap_string(m->Mn, 32);
	swap_string(m->Md, 32);
	swap_string(m->Opt, 16);
	swap_string(m->Vr, 16);
	swap_string(m->SN, 32);
}

static void map_extended(sunspec_extended_t *m) {
	swap_string(m->TmSrc, 8);
}

static void map_mppt(sunspec_mppt_t *m) {
	swap_string(m->IDStr1, 16);
	swap_string(m->IDStr2, 16);
	SWAP32(m->DCWH1);
	SWAP32(m->DCWH2);
}

static void map_meter(sunspec_meter_t *m) {
	SWAP32(m->TotWhExp);
	SWAP32(m->TotWhImp);
}

static void collect_models(sunspec_t *ss) {
	uint32_t sunspec_id;

	uint16_t index[2] = { 0, 0 };
	uint16_t *id = &index[0];
	uint16_t *size = &index[1];

	int address = SUNSPEC_BASE_ADDRESS;
	modbus_read_registers(ss->mb, address, 2, (uint16_t*) &sunspec_id);

	// 0x53756e53 = 'SunS'
	SWAP32(sunspec_id);
	if (sunspec_id != 0x53756e53) {
		xlog("SUNSPEC %s no 'SunS' found at address %d", ss->name, address);
		return;
	}

	xlog("SUNSPEC %s found 'SunS' at address %d", ss->name, address);
	address += 2;

	while (1) {
		modbus_read_registers(ss->mb, address, 2, (uint16_t*) &index);

		if (*id == 0xffff && *size == 0)
			break;

		switch (*id) {

		case 001:
			ss->common_addr = address;
			ss->common_size = *size;
			ss->common_id = *id;
			if (!ss->common)
				ss->common = malloc(sizeof(sunspec_common_t));
			ZEROP(ss->common);
			xlog("SUNSPEC %s found Common(%03d) model size %d at address %d", ss->name, *id, *size, address);
			break;

		case 101:
		case 102:
		case 103:
			ss->inverter_addr = address;
			ss->inverter_size = *size;
			ss->inverter_id = *id;
			if (!ss->inverter)
				ss->inverter = malloc(sizeof(sunspec_inverter_t));
			ZEROP(ss->inverter);
			xlog("SUNSPEC %s found Inverter(%03d) model size %d at address %d", ss->name, *id, *size, address);
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
			xlog("SUNSPEC %s found Meter(%03d) model size %d at address %d", ss->name, *id, *size, address);
			break;

		case 120:
			ss->nameplate_addr = address;
			ss->nameplate_size = *size;
			ss->nameplate_id = *id;
			if (!ss->nameplate)
				ss->nameplate = malloc(sizeof(sunspec_nameplate_t));
			ZEROP(ss->nameplate);
			xlog("SUNSPEC %s found Nameplate(%03d) model size %d at address %d", ss->name, *id, *size, address);
			break;

		case 121:
			ss->basic_addr = address;
			ss->basic_size = *size;
			ss->basic_id = *id;
			if (!ss->basic)
				ss->basic = malloc(sizeof(sunspec_basic_t));
			ZEROP(ss->basic);
			xlog("SUNSPEC %s found Basic(%03d) model size %d at address %d", ss->name, *id, *size, address);
			break;

		case 122:
			ss->extended_addr = address;
			ss->extended_size = *size;
			ss->extended_id = *id;
			if (!ss->extended)
				ss->extended = malloc(sizeof(sunspec_extended_t));
			ZEROP(ss->extended);
			xlog("SUNSPEC %s found Extended(%03d) model size %d at address %d", ss->name, *id, *size, address);
			break;

		case 123:
			ss->immediate_addr = address;
			ss->immediate_size = *size;
			ss->immediate_id = *id;
			if (!ss->immediate)
				ss->immediate = malloc(sizeof(sunspec_immediate_t));
			ZEROP(ss->immediate);
			xlog("SUNSPEC %s found Immediate(%03d) model size %d at address %d", ss->name, *id, *size, address);
			break;

		case 124:
			ss->storage_addr = address;
			ss->storage_size = *size;
			ss->storage_id = *id;
			if (!ss->storage)
				ss->storage = malloc(sizeof(sunspec_storage_t));
			ZEROP(ss->storage);
			xlog("SUNSPEC %s found Storage(%03d) model size %d at address %d", ss->name, *id, *size, address);
			break;

		case 160:
			ss->mppt_addr = address;
			ss->mppt_size = *size;
			ss->mppt_id = *id;
			if (!ss->mppt)
				ss->mppt = malloc(sizeof(sunspec_mppt_t));
			ZEROP(ss->mppt);
			xlog("SUNSPEC %s found MPPT(%03d) model size %d at address %d", ss->name, *id, *size, address);
			break;

		default:
			xlog("SUNSPEC %s unknown model %d size %d at address %d", ss->name, *id, *size, address);
		}
		address += *size + 2;
	}
}

static int read_model(sunspec_t *ss, uint16_t id, uint16_t addr, uint16_t size, uint16_t *model) {
	// xdebug("SUNSPEC %s read_model %d", ss->name, id);

	// zero model
	memset(model, 0, size * sizeof(uint16_t) + 2);

	// read
	int rc = modbus_read_registers(ss->mb, addr, size + 2, model);
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

		// read static models once here

		if (ss->common) {
			errors += read_model(ss, ss->common_id, ss->common_addr, ss->common_size, (uint16_t*) ss->common);
			map_common(ss->common);
			xlog("SUNSPEC %s found %s %s (%s) version %s serial %s", ss->name, ss->common->Mn, ss->common->Md, ss->common->Opt, ss->common->Vr, ss->common->SN);
		}

		if (ss->nameplate)
			errors += read_model(ss, ss->nameplate_id, ss->nameplate_addr, ss->nameplate_size, (uint16_t*) ss->nameplate);

		if (ss->basic)
			errors += read_model(ss, ss->basic_id, ss->basic_addr, ss->basic_size, (uint16_t*) ss->basic);

		if (ss->extended) {
			errors += read_model(ss, ss->extended_id, ss->extended_addr, ss->extended_size, (uint16_t*) ss->extended);
			map_extended(ss->extended);
		}

		if (ss->immediate)
			errors += read_model(ss, ss->immediate_id, ss->immediate_addr, ss->immediate_size, (uint16_t*) ss->immediate);

		// read dynamic models in a loop
		while (errors > -10) {
			msleep(ss->poll);

			if (ss->inverter)
				errors += read_model(ss, ss->inverter_id, ss->inverter_addr, ss->inverter_size, (uint16_t*) ss->inverter);

			if (ss->mppt) {
				errors += read_model(ss, ss->mppt_id, ss->mppt_addr, ss->mppt_size, (uint16_t*) ss->mppt);
				map_mppt(ss->mppt);
			}

			if (ss->storage)
				errors += read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);

			if (ss->meter) {
				errors += read_model(ss, ss->meter_id, ss->meter_addr, ss->meter_size, (uint16_t*) ss->meter);
				map_meter(ss->meter);
			}

			// execute the callback function to process model data
			if (ss->callback)
				(ss->callback)(ss);

			// xdebug("SUNSPEC %s meter grid %d", ss->name, ss->meter->W);
			// xdebug("SUNSPEC %s poll time %d", ss->name, ss->poll_time_ms);
		}

		xlog("SUNSPEC aborting %s poll due to too many errors");

		modbus_close(ss->mb);
		modbus_free(ss->mb);
		ss->mb = 0;
	}

	return (void*) 0;
}

void sunspec_write_reg(sunspec_t *ss, int addr, const uint16_t value) {
	int rc = modbus_write_register(ss->mb, addr, value);
	if (rc == -1)
		xerr("SUNSPEC %s modbus_write_register %s", ss->name, modbus_strerror(errno));
}

void sunspec_read_reg(sunspec_t *ss, int addr, uint16_t *value) {
	int rc = modbus_read_registers(ss->mb, addr, 1, value);
	if (rc == -1)
		xerr("SUNSPEC %s modbus_read_registers %s", ss->name, modbus_strerror(errno));
}

void sunspec_read(sunspec_t *ss) {
	if (ss->inverter)
		read_model(ss, ss->inverter_id, ss->inverter_addr, ss->inverter_size, (uint16_t*) ss->inverter);

	if (ss->mppt)
		read_model(ss, ss->mppt_id, ss->mppt_addr, ss->mppt_size, (uint16_t*) ss->mppt);

	if (ss->storage)
		read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);

	if (ss->meter)
		read_model(ss, ss->meter_id, ss->meter_addr, ss->meter_size, (uint16_t*) ss->meter);
}

sunspec_t* sunspec_init(const char *name, const char *ip, int slave) {
	sunspec_t *ss = malloc(sizeof(sunspec_t));
	ZEROP(ss);

	ss->ip = ip;
	ss->name = name;
	ss->slave = slave;
	ss->poll = 0;

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

sunspec_t* sunspec_init_poll(const char *name, const char *ip, int slave, const sunspec_callback_t callback) {
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

	xlog("SUNSPEC stopped %s", ss->name);
	free(ss);
}

int sunspec_storage_limit_discharge(sunspec_t *ss, int wcha) {
	if (!ss->storage)
		return 0;

	if (!ss->thread)
		read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);

	int sf = ss->storage->InOutWRte_SF;
	int wchaMax = SFI(ss->storage->WchaMax, ss->storage->WchaMax_SF);

	if (wcha < 0)
		wcha = 0;
	if (wcha > wchaMax)
		wcha = wchaMax;

	int limit_sf = SF_OUT(wcha, sf) * 100 / wchaMax;

	if (ss->storage->StorCtl_Mod == 2 && ss->storage->OutWRte == limit_sf)
		return 0; // already set

	xlog("SUNSPEC set discharge limit to %d W", wcha);
	sunspec_write_reg(ss, ss->storage_addr + storage_offset.StorCtl_Mod, 2);
	sunspec_write_reg(ss, ss->storage_addr + storage_offset.OutWRte, limit_sf);
	read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);
	xlog("SUNSPEC StorCtl_Mod=%d InWRte=%.1f OutWRte=%.1f WchaMax=%d", ss->storage->StorCtl_Mod, SFF(ss->storage->InWRte, sf), SFF(ss->storage->OutWRte, sf), wchaMax);

	return ss->storage->StorCtl_Mod == 2 ? 0 : -1;
}

int sunspec_storage_limit_charge(sunspec_t *ss, int wcha) {
	if (!ss->storage)
		return 0;

	if (!ss->thread)
		read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);

	int sf = ss->storage->InOutWRte_SF;
	int wchaMax = SFI(ss->storage->WchaMax, ss->storage->WchaMax_SF);

	if (wcha < 0)
		wcha = 0;
	if (wcha > wchaMax)
		wcha = wchaMax;

	int limit_sf = SF_OUT(wcha, sf) * 100 / wchaMax;

	if (ss->storage->StorCtl_Mod == 1 && ss->storage->InWRte == limit_sf)
		return 0; // already set

	xlog("SUNSPEC set charge limit to %d W", wcha);
	sunspec_write_reg(ss, ss->storage_addr + storage_offset.StorCtl_Mod, 1);
	sunspec_write_reg(ss, ss->storage_addr + storage_offset.InWRte, limit_sf);
	read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);
	xlog("SUNSPEC StorCtl_Mod=%d InWRte=%.1f OutWRte=%.1f WchaMax=%d", ss->storage->StorCtl_Mod, SFF(ss->storage->InWRte, sf), SFF(ss->storage->OutWRte, sf), wchaMax);

	return ss->storage->StorCtl_Mod == 1 ? 0 : -1;
}

int sunspec_storage_limit_reset(sunspec_t *ss) {
	if (!ss->storage)
		return 0;

	if (!ss->thread)
		read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);

	int sf = ss->storage->InOutWRte_SF;
	int wchaMax = SFI(ss->storage->WchaMax, ss->storage->WchaMax_SF);
	int limit_sf = SF_OUT(100, sf);

	if (ss->storage->StorCtl_Mod == 0 && ss->storage->InWRte == limit_sf && ss->storage->OutWRte == limit_sf)
		return 0; // already set

	xlog("SUNSPEC reset charge/discharge limits");
	sunspec_write_reg(ss, ss->storage_addr + storage_offset.StorCtl_Mod, 0);
	sunspec_write_reg(ss, ss->storage_addr + storage_offset.InWRte, limit_sf);
	sunspec_write_reg(ss, ss->storage_addr + storage_offset.OutWRte, limit_sf);
	read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);
	xlog("SUNSPEC StorCtl_Mod=%d InWRte=%d OutWRte=%d WchaMax=%d", ss->storage->StorCtl_Mod, SFI(ss->storage->InWRte, sf), SFI(ss->storage->OutWRte, sf), wchaMax);

	return ss->storage->StorCtl_Mod == 0 ? 0 : -1;
}

int sunspec_storage_minimum_soc(sunspec_t *ss, int soc) {
	if (!ss->storage)
		return 0;

	if (!ss->thread)
		read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);

	int soc_sf = SF_OUT(soc, ss->storage->MinRsvPct_SF);
	if (ss->storage->MinRsvPct == soc_sf)
		return 0; // already set

	xlog("SUNSPEC setting minimum SoC to %d", soc);
	sunspec_write_reg(ss, ss->storage_addr + storage_offset.MinRsvPct, soc_sf);
	read_model(ss, ss->storage_id, ss->storage_addr, ss->storage_size, (uint16_t*) ss->storage);
	xlog("SUNSPEC MinRsvPct=%d", SFI(ss->storage->MinRsvPct, ss->storage->MinRsvPct_SF));

	return ss->storage->MinRsvPct == soc_sf ? 0 : -1;
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
		// modbus_set_slave(mb, 200);
		// modbus_read_registers(mb, METER_OFFSET - 1, METER_SIZE, (uint16_t*) &meter);

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
