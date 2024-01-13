#define ON				"ON"
#define OFF				"OFF"

// Shelly Plugs
#define PLUG1			0x810F43
#define PLUG2			0x81420A
#define PLUG3			0x814D47
#define PLUG4			0x83185A
#define PLUG5			0xB60A0C
#define PLUG6			0x5E40EC
#define PLUG7			0xC24A88
#define PLUG8			0x58ED80
#define PLUG9			0x5EEEE8

// Shelly Switches
#define HOFLICHT		0x2FEFEE

// Shelly Buttons
#define KUECHE			0xB20670

// Shelly Shutters
#define ROLLO_KUECHE	0xA2950C
#define ROLLO_OMA		0xA0F584
#define ROLLO_O			0x111111
#define ROLLO_SO		0x222222
#define ROLLO_SW		0x333333
#define ROLLO_W			0x444444

#define SHUTTER_UP		100
#define SHUTTER_HALF	50
#define SHUTTER_DOWN	0
#define SHUTTER_POS		-1

typedef struct tasmota_config_t {
	const unsigned int id;
	const unsigned int relay;
	const unsigned int t1;
	const unsigned int t1b;
	const unsigned int t2;
	const unsigned int t2b;
	const unsigned int t3;
	const unsigned int t3b;
	const unsigned int t4;
	const unsigned int t4b;
	const unsigned int timer;
} tasmota_config_t;

typedef struct tasmota_state_t {
	unsigned int id;
	int relay1;
	int relay2;
	int position;
	unsigned int timer;
	void *next;
} tasmota_state_t;

void tasmota_power(unsigned int, int, int);

void tasmota_shutter(unsigned int, unsigned int);

void tasmota_dispatch(const char*, uint16_t, const char*, size_t);
