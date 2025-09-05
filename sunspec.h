#include <modbus/modbus.h>

#define SFF(x, y)				(y == 0 ? x : (x) * pow(10, y))
#define SFI(x, y)				(y == 0 ? x : (int)((x) * pow(10, y)))
#define SFUI(x, y)				(y == 0 ? x : (unsigned int)((x) * pow(10, y)))
#define SFOUT(x, y)				(y == 0 ? x : (x) / pow(10, y))

#define SUNSPEC_BASE_ADDRESS	40000

#define OFFSET(model, reg)		(((void*) &reg - (void*) model) / 2)

// SunSpec Inverter States
#define I_STATUS_OFF 			1		// Wechselrichter ist aus
#define I_STATUS_SLEEPING 		2		// Auto-Shutdown
#define I_STATUS_STARTING		3		// Wechselrichter startet
#define I_STATUS_MPPT			4		// Wechselrichter arbeitet normal
#define I_STATUS_THROTTLED		5		// Leistungsreduktion aktiv
#define I_STATUS_SHUTTING_DOWN	6		// Wechselrichter schaltet ab
#define I_STATUS_FAULT			7		// Ein oder mehr Fehler existieren, siehe St *oder Evt * Register
#define I_STATUS_STANDBY		8		// Standby
#define I_STATUS_NO_BUSINIT		9		// Keine SolarNet Kommunikation
#define I_STATUS_NO_COMM_INV	10		// Keine Kommunikation mit Wechselrichter möglich
#define I_STATUS_SN_OVERCURRENT	11		// Überstrom an SolarNet Stecker erkannt
#define I_STATUS_BOOTLOAD		12		// Wechselrichter wird gerade up-gedatet
#define I_STATUS_AFCI			13		// AFCI Event (Arc-Erkennung)

#define CONNECT_RETRY_TIME		900 	// seconds
#define SLEEP_TIME_FAULT		10		// 10 sec
#define SLEEP_TIME_SLEEPING		900		// 15 min

#define STORAGE_LIMIT_BOTH		3
#define STORAGE_LIMIT_DISCHARGE	2
#define STORAGE_LIMIT_CHARGE	1
#define STORAGE_LIMIT_NONE		0

//
// sunspec models generated from Fronius documentation copied to misc/sunspec-struct-template.ods
//

typedef struct sunspec_common_t {
	uint16_t ID;
	uint16_t L;
	char Mn[32];
	char Md[32];
	char Opt[16];
	char Vr[16];
	char SN[32];
	uint16_t DA;
} sunspec_common_t;
#define SUNSPEC_COMMON_SIZE				(sizeof(sunspec_common_t))

typedef struct sunspec_nameplate_t {
	uint16_t ID;
	uint16_t L;
	uint16_t DERTyp;
	uint16_t WRtg;
	int16_t WRtg_SF;
	uint16_t VARtg;
	int16_t VARtg_SF;
	int16_t VArRtgQ1;
	int16_t VArRtgQ2;
	int16_t VArRtgQ3;
	int16_t VArRtgQ4;
	int16_t VArRtg_SF;
	uint16_t ARtg;
	int16_t ARtg_SF;
	int16_t PFRtgQ1;
	int16_t PFRtgQ2;
	int16_t PFRtgQ3;
	int16_t PFRtgQ4;
	int16_t PFRtg_SF;
	uint16_t WHRtg;
	int16_t WHRtg_SF;
	uint16_t AhrRtg;
	int16_t AhrRtg_SF;
	uint16_t MaxChaRte;
	int16_t MaxChaRte_SF;
	uint16_t MaxDisChaRte;
	int16_t MaxDisChaRte_SF;
	int16_t pad;
} sunspec_nameplate_t;
#define SUNSPEC_NAMEPLATE_SIZE			(sizeof(sunspec_nameplate_t))

typedef struct sunspec_settings_t {
	uint16_t ID;
	uint16_t L;
	uint16_t WMax;
	uint16_t VRef;
	int16_t VRefOfs;
	uint16_t VMax;
	uint16_t VMin;
	uint16_t VAMax;
	int16_t VArMaxQ1;
	int16_t VArMaxQ2;
	int16_t VArMaxQ3;
	int16_t VArMaxQ4;
	uint16_t WGra;
	int16_t PFMinQ1;
	int16_t PFMinQ2;
	int16_t PFMinQ3;
	int16_t PFMinQ4;
	uint16_t VArAct;
	uint16_t ClcTotVA;
	uint16_t MaxRmpRte;
	uint16_t ECPNomHz;
	uint16_t ConnPh;
	int16_t WMax_SF;
	int16_t VRef_SF;
	int16_t VRefOfs_SF;
	int16_t VMinMax_SF;
	int16_t VAMax_SF;
	int16_t VArMax_SF;
	int16_t WGra_SF;
	int16_t PFMin_SF;
	int16_t MaxRmpRte_SF;
	int16_t ECPNomHz_SF;
} sunspec_settings_t;
#define SUNSPEC_SETTINGS_SIZE				(sizeof(sunspec_settings_t))

typedef struct sunspec_status_t {
	uint16_t ID;
	uint16_t L;
	uint16_t PVConn;
	uint16_t StorConn;
	uint16_t ECPConn;
	uint64_t ActWh;
	uint64_t ActVAh;
	uint64_t ActVArhQ1;
	uint64_t ActVArhQ2;
	uint64_t ActVArhQ3;
	uint64_t ActVArhQ4;
	int16_t VArAval;
	int16_t VArAval_SF;
	uint16_t WAval;
	int16_t WAval_SF;
	uint32_t StSetLimMsk;
	uint32_t StActCtl;
	char TmSrc[8];
	uint32_t Tms;
	uint16_t RtSt;
	uint16_t Ris;
	int16_t Ris_SF;
} sunspec_status_t;
#define SUNSPEC_STATUS_SIZE				(sizeof(sunspec_status_t))

typedef struct sunspec_controls_t {
	uint16_t ID;
	uint16_t L;
	uint16_t Conn_WinTms;
	uint16_t Conn_RvrtTms;
	uint16_t Conn;
	uint16_t WMaxLimPct;
	uint16_t WMaxLimPct_WinTms;
	uint16_t WMaxLimPct_RvrtTms;
	uint16_t WMaxLimPct_RmpTms;
	uint16_t WMaxLim_Ena;
	int16_t OutPFSet;
	uint16_t OutPFSet_WinTms;
	uint16_t OutPFSet_RvrtTms;
	uint16_t OutPFSet_RmpTms;
	uint16_t OutPFSet_Ena;
	int16_t VArWMaxPct;
	int16_t VArMaxPct;
	int16_t VArAvalPct;
	uint16_t VArPct_WinTms;
	uint16_t VArPct_RvrtTms;
	uint16_t VArPct_RmpTms;
	uint16_t VArPct_Mod;
	uint16_t VArPct_Ena;
	int16_t WMaxLimPct_SF;
	int16_t OutPFSet_SF;
	int16_t VArPct_SF;
} sunspec_controls_t;
#define SUNSPEC_CONTROLS_SIZE			(sizeof(sunspec_controls_t))

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
#define SUNSPEC_INVERTER_SIZE			(sizeof(sunspec_inverter_t))

typedef struct sunspec_mppt_t {
	uint16_t ID;
	uint16_t L;
	int16_t DCA_SF;
	int16_t DCV_SF;
	int16_t DCW_SF;
	int16_t DCWH_SF;
	uint32_t Evt;
	int16_t N;
	uint16_t TmsPer;
	uint16_t m1_ID;
	char m1_IDStr[16];
	uint16_t m1_DCA;
	uint16_t m1_DCV;
	uint16_t m1_DCW;
	uint32_t m1_DCWH;
	uint32_t m1_Tms;
	int16_t m1_Tmp;
	uint16_t m1_DCSt;
	uint32_t m1_DCEvt;
	uint16_t m2_ID;
	char m2_IDStr[16];
	uint16_t m2_DCA;
	uint16_t m2_DCV;
	uint16_t m2_DCW;
	uint32_t m2_DCWH;
	uint32_t m2_Tms;
	int16_t m2_Tmp;
	uint16_t m2_DCSt;
	uint32_t m2_DCEvt;
	uint16_t m3_ID;
	char m3_IDStr[16];
	uint16_t m3_DCA;
	uint16_t m3_DCV;
	uint16_t m3_DCW;
	uint32_t m3_DCWH;
	uint32_t m3_Tms;
	int16_t m3_Tmp;
	uint16_t m3_DCSt;
	uint32_t m3_DCEvt;
	uint16_t m4_ID;
	char m4_IDStr[16];
	uint16_t m4_DCA;
	uint16_t m4_DCV;
	uint16_t m4_DCW;
	uint32_t m4_DCWH;
	uint32_t m4_Tms;
	int16_t m4_Tmp;
	uint16_t m4_DCSt;
	uint32_t m4_DCEvt;
} sunspec_mppt_t;
#define SUNSPEC_MPPT_SIZE				(sizeof(sunspec_mppt_t))

typedef struct sunspec_storage_t {
	uint16_t ID;
	uint16_t L;
	uint16_t WChaMax;
	uint16_t WChaGra;
	uint16_t WDisChaGra;
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
	int16_t WChaMax_SF;
	int16_t WChaDisChaGra_SF;
	int16_t VAChaMax_SF;
	int16_t MinRsvPct_SF;
	int16_t ChaState_SF;
	int16_t StorAval_SF;
	int16_t InBatV_SF;
	int16_t InOutWRte_SF;
} sunspec_storage_t;
#define SUNSPEC_STORAGE_SIZE			(sizeof(sunspec_storage_t))

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
	int16_t PhVphAB;
	int16_t PhVphBC;
	int16_t PhVphCA;
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
	uint32_t TotVArhImpQ1PhA;
	uint32_t TotVArhImpQ1PhB;
	uint32_t TotVArhImpQ1PhC;
	uint32_t TotVArhImpQ2;
	uint32_t TotVArhImpQ2PhA;
	uint32_t TotVArhImpQ2PhB;
	uint32_t TotVArhImpQ2PhC;
	uint32_t TotVArhExpQ3;
	uint32_t TotVArhExpQ3PhA;
	uint32_t TotVArhExpQ3PhB;
	uint32_t TotVArhExpQ3PhC;
	uint32_t TotVArhExpQ4;
	uint32_t TotVArhExpQ4PhA;
	uint32_t TotVArhExpQ4PhB;
	uint32_t TotVArhExpQ4PhC;
	int16_t TotVArh_SF;
	uint32_t Evt;
} sunspec_meter_t;
#define SUNSPEC_METER_SIZE				(sizeof(sunspec_meter_t))

typedef struct _sunspec sunspec_t;

typedef void (*sunspec_callback_t)(sunspec_t *ss);

struct _sunspec {
	const char *name;
	const char *ip;
	int slave;
	int sleep;
	int control;
	time_t ts;
	sunspec_callback_t callback;
	pthread_t thread;
	pthread_mutex_t lock;
	modbus_t *mb;

	sunspec_common_t *common;
	uint16_t common_addr;
	uint16_t common_size;
	uint16_t common_id;

	sunspec_nameplate_t *nameplate;
	uint16_t nameplate_addr;
	uint16_t nameplate_size;
	uint16_t nameplate_id;

	sunspec_settings_t *settings;
	uint16_t settings_addr;
	uint16_t settings_size;
	uint16_t settings_id;

	sunspec_status_t *status;
	uint16_t status_addr;
	uint16_t status_size;
	uint16_t status_id;

	sunspec_controls_t *controls;
	uint16_t controls_addr;
	uint16_t controls_size;
	uint16_t controls_id;

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

sunspec_t* sunspec_init_poll(const char *name, int slave, const sunspec_callback_t callback);
sunspec_t* sunspec_init(const char *name, int slave);

void sunspec_write_reg(sunspec_t *ss, int addr, const uint16_t value);
void sunspec_read_reg(sunspec_t *ss, int addr, uint16_t *value);

int sunspec_read(sunspec_t *ss);
void sunspec_stop(sunspec_t *ss);

int sunspec_storage_limit_both(sunspec_t *ss, int inWRte, int outWRte);
int sunspec_storage_limit_charge(sunspec_t *ss, int wcha);
int sunspec_storage_limit_discharge(sunspec_t *ss, int inWRte);
int sunspec_storage_limit_reset(sunspec_t *ss);
int sunspec_storage_minimum_soc(sunspec_t *ss, int soc);
