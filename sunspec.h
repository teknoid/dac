#include <modbus/modbus.h>

// modbus address of the first SunSpec model
#define SUNSPEC_BASE_ADDRESS	40070

// SunSpec Inverter States
#define I_STATUS_OFF 			1	// Wechselrichter ist aus
#define I_STATUS_SLEEPING 		2	// Auto-Shutdown
#define I_STATUS_STARTING		3	// Wechselrichter startet
#define I_STATUS_MPPT			4	// Wechselrichter arbeitet normal
#define I_STATUS_THROTTLED		5	// Leistungsreduktion aktiv
#define I_STATUS_SHUTTING_DOWN	6	// Wechselrichter schaltet ab
#define I_STATUS_FAULT			7	// Ein oder mehr Fehler existieren, siehe St *oder Evt * Register
#define I_STATUS_STANDBY		8	// Standby
#define I_STATUS_NO_BUSINIT		9	// Keine SolarNet Kommunikation
#define I_STATUS_NO_COMM_INV	10	// Keine Kommunikation mit Wechselrichter möglich
#define I_STATUS_SN_OVERCURRENT	11	// Überstrom an SolarNet Stecker erkannt
#define I_STATUS_BOOTLOAD		12	// Wechselrichter wird gerade up-gedatet
#define I_STATUS_AFCI			13	// AFCI Event (Arc-Erkennung)

#define CONNECT_RETRY_TIME		900 		// seconds
#define POLL_TIME_ACTIVE		500 		// milliseconds
#define POLL_TIME_FAULT			1000 * 60	// 1 min
#define POLL_TIME_SLEEPING		1000 * 900 	// 15 min

//
// sunspec models generated from Fronius documentation copied to misc/sunspec-struct-template.ods
//

typedef struct sunspec_inverter_t {
	uint16_t ID;
	uint16_t L;
	uint16_t A;
	uint16_t AphA;
	uint16_t AphB;
	uint16_t AphC;
	int16_t A_SF;
	uint16_t PPVphAB;
	uint16_t PPVphBC;
	uint16_t PPVphCA;
	uint16_t PhVphA;
	uint16_t PhVphB;
	uint16_t PhVphC;
	int16_t V_SF;
	int16_t W;
	int16_t W_SF;
	uint16_t Hz;
	int16_t Hz_SF;
	int16_t VA;
	int16_t VA_SF;
	int16_t VAr;
	int16_t VAr_SF;
	int16_t PF;
	int16_t PF_SF;
	uint32_t WH;
	int16_t WH_SF;
	uint16_t DCA;
	int16_t DCA_SF;
	uint16_t DCV;
	int16_t DCV_SF;
	int16_t DCW;
	int16_t DCW_SF;
	int16_t TmpCab;
	int16_t TmpSnk;
	int16_t TmpTrns;
	int16_t TmpOt;
	int16_t Tmp_SF;
	uint16_t St;
	uint16_t StVnd;
	uint32_t Evt1;
	uint32_t Evt2;
	uint32_t EvtVnd1;
	uint32_t EvtVnd2;
	uint32_t EvtVnd3;
	uint32_t EvtVnd4;
} sunspec_inverter_t;

typedef struct sunspec_mppt_t {
	uint16_t ID;
	uint16_t L;
	int16_t DCA_SF;
	int16_t DCV_SF;
	int16_t DCW_SF;
	int16_t DCWH_SF;
	uint32_t Evt;
	uint16_t N;
	uint16_t TmsPer;
	uint16_t ID1;
	char IDStr1[16];
	uint16_t DCA1;
	uint16_t DCV1;
	uint16_t DCW1;
	uint32_t DCWH1;
	uint32_t Tms1;
	int16_t Tmp1;
	uint16_t DCSt1;
	uint32_t DCEvt1;
	uint16_t ID2;
	char IDStr2[16];
	uint16_t DCA2;
	uint16_t DCV2;
	uint16_t DCW2;
	uint32_t DCWH2;
	uint32_t Tms2;
	int16_t Tmp2;
	uint16_t DCSt2;
	uint32_t DCEvt2;
} sunspec_mppt_t;

typedef struct sunspec_storage_t {
	uint16_t ID;
	uint16_t L;
	uint16_t WchaMax;
	uint16_t WchaGra;
	uint16_t WdisChaGra;
	uint16_t StorCtl_Mod;
	uint16_t VAChaMax;
	uint16_t MinRsvPct;
	uint16_t ChaState;
	uint16_t StorAval;
	uint16_t InBatV;
	uint16_t ChaSt;
	int16_t OutWRte;
	int16_t InWRte;
	uint16_t InOutWRte_WinTms;
	uint16_t InOutWRte_RvrtTms;
	uint16_t InOutWRte_RmpTms;
	uint16_t ChaGriSet;
	int16_t WchaMax_SF;
	int16_t WchaDisChaGra_SF;
	int16_t VAChaMax_SF;
	int16_t MinRsvPct_SF;
	int16_t ChaState_SF;
	int16_t StorAval_SF;
	int16_t InBatV_SF;
	int16_t InOutWRte_SF;
} sunspec_storage_t;

typedef struct sunspec_meter_t {
	uint16_t ID;
	uint16_t L;
	int16_t A;
	int16_t AphA;
	int16_t AphB;
	int16_t AphC;
	int16_t A_SF;
	int16_t PhV;
	int16_t PhVphA;
	int16_t PhVphB;
	int16_t PhVphC;
	int16_t PPV;
	int16_t PPVphAB;
	int16_t PPVphBC;
	int16_t PPVphCA;
	int16_t V_SF;
	int16_t Hz;
	int16_t Hz_SF;
	int16_t W;
	int16_t WphA;
	int16_t WphB;
	int16_t WphC;
	int16_t W_SF;
	int16_t VA;
	int16_t VAphA;
	int16_t VAphB;
	int16_t VAphC;
	int16_t VA_SF;
	int16_t VAR;
	int16_t VARphA;
	int16_t VARphB;
	int16_t VARphC;
	int16_t VAR_SF;
	int16_t PF;
	int16_t PFphA;
	int16_t PFphB;
	int16_t PFphC;
	int16_t PF_SF;
	uint32_t TotWhExp;
	uint32_t TotWhExpPhA;
	uint32_t TotWhExpPhB;
	uint32_t TotWhExpPhC;
	uint32_t TotWhImp;
	uint32_t TotWhImpPhA;
	uint32_t TotWhImpPhB;
	uint32_t TotWhImpPhC;
	int16_t TotWh_SF;
	uint32_t TotVAhExp;
	uint32_t TotVAhExpPhA;
	uint32_t TotVAhExpPhB;
	uint32_t TotVAhExpPhC;
	uint32_t TotVAhImp;
	uint32_t TotVAhImpPhA;
	uint32_t TotVAhImpPhB;
	uint32_t TotVAhImpPhC;
	int16_t TotVAh_SF;
	uint32_t TotVArhImpQ1;
	uint32_t TotVArhImpQ1phA;
	uint32_t TotVArhImpQ1phB;
	uint32_t TotVArhImpQ1phC;
	uint32_t TotVArhImpQ2;
	uint32_t TotVArhImpQ2phA;
	uint32_t TotVArhImpQ2phB;
	uint32_t TotVArhImpQ2phC;
	uint32_t TotVArhExpQ3;
	uint32_t TotVArhExpQ3phA;
	uint32_t TotVArhExpQ3phB;
	uint32_t TotVArhExpQ3phC;
	uint32_t TotVArhExpQ4;
	uint32_t TotVArhExpQ4phA;
	uint32_t TotVArhExpQ4phB;
	uint32_t TotVArhExpQ4phC;
	int16_t TotVArh_SF;
	uint32_t Evt;
} sunspec_meter_t;

typedef struct _sunspec sunspec_t;

typedef void (*sunspec_callback_t)(sunspec_t *ss);

struct _sunspec {
	const char *name;
	const char *ip;
	int slave;
	int poll;
	int active;
	sunspec_callback_t callback;
	pthread_t thread;
	modbus_t *mb;

	sunspec_inverter_t *inverter;
	uint16_t inverter_addr;
	uint16_t inverter_size;
	uint16_t inverter_id;

	sunspec_mppt_t *mppt;
	uint16_t mppt_addr;
	uint16_t mppt_size;
	uint16_t mppt_id;

	sunspec_storage_t *storage;
	uint16_t storage_addr;
	uint16_t storage_size;
	uint16_t storage_id;

	sunspec_meter_t *meter;
	uint16_t meter_addr;
	uint16_t meter_size;
	uint16_t meter_id;
};

sunspec_t* sunspec_init_poll(const char *name, const char *ip, int slave, const sunspec_callback_t callback);
sunspec_t* sunspec_init(const char *name, const char *ip, int slave);

void sunspec_write_reg(sunspec_t *ss, int addr, const uint16_t value);
void sunspec_read_reg(sunspec_t *ss, int addr, uint16_t *value);

void sunspec_read(sunspec_t *ss);
void sunspec_stop(sunspec_t *ss);
