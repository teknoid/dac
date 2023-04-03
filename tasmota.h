#define ON				"ON"
#define OFF				"OFF"

// Relays
#define PLUG1			0x810F43
#define PLUG2			0x123456
#define HOFLICHT		0x2FEFEE

// Switches
#define KUECHE			0xB20670

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
	unsigned int relay;
	unsigned int state;
	unsigned int timer;
	void *next;
} tasmota_state_t;

void tasmota_command(unsigned int, int, int);

int tasmota_dispatch(const char*, uint16_t, const char*, size_t);
