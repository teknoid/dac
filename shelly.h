#define ON				"ON"
#define OFF				"OFF"

#define PLUG1			0x810F43
#define PLUG2			0x123456
#define HOFLICHT		0x2FEFEE

#define VIER_KUECHE		0xB20670

typedef struct shelly_t {
	const unsigned int id;
	const unsigned int relay;
	const unsigned int trigger1;
	const unsigned int trigger1_button;
	const unsigned int trigger2;
	const unsigned int trigger2_button;
	const unsigned int trigger3;
	const unsigned int trigger3_button;
	const unsigned int timer_start;
	unsigned int timer;
	unsigned int state;
} shelly_t;

void shelly_command(unsigned int, int);

int shelly_dispatch(const char*, uint16_t, const char*, size_t);
