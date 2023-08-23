// odroid
// #define TX				219
// #define RX				247

// picam
#define TX				"GPIO04"
#define RX				"GPIO17"

// pidev
// #define TX					"GPIO17"
// #define RX					"GPIO27"

// transmitter id's of our known remote control units
const static uint16_t REMOTES[5] = { 0x53cc, 0x835a, 0x31e2, 0x295c, 0x272d };
//									 White1  White2  White3  Black   SF500

// timings for 28bit rc1 and 24bit rc4 patterns
// tested with 1 C 1: min 180 max 350 --> 330 is closest to the original remote
#define T1					330
const static uint16_t T1X2 = T1 * 2;
const static uint16_t T1X3 = T1 * 3;
const static uint16_t T1X15 = T1 * 15;
const static uint16_t T1X31 = T1 * 31;
const static uint16_t T1SMIN = T1X15 - 80;
const static uint16_t T1SMAX = T1X15 + 80;
const static uint16_t T4SMIN = T1X31 - 100;
const static uint16_t T4SMAX = T1X31 + 100;

// timings for 32bit rc2 patterns
#define T2H					200
#define T2L					330
const static uint16_t T2X = (T2H + T2L) * 2 + T2L; // 1390 - low data bit delay
const static uint16_t T2Y = T2X / 2; // 695 - decides 0 / 1
const static uint16_t T2S1 = T2X * 2; // 2780 - low sync delay (FA500R)
const static uint16_t T2S1MIN = T2S1 - 50;
const static uint16_t T2S1MAX = T2S1 + 50;
const static uint16_t T2S2 = T2L * 8; // 2640 - low sync delay (SF500R)
const static uint16_t T2S2MIN = T2S2 - 50;
const static uint16_t T2S2MAX = T2S2 + 50;

// timings for 32bit rc3 multibit patterns
#define T3H					220
#define T3L					330
const static uint16_t T3X = T3H + T3L + T3L; // 880 - low delay to next clock
const static uint16_t T3Y = T3H + T3L; // 550 - decides if clock or data bit
const static uint16_t T3S = 9250; // don't know how to calculate
const static uint16_t T3SMIN = T3S - 50;
const static uint16_t T3SMAX = T3S + 50;

#define SPACEMASK_FA500		0x01000110
#define SPACEMASK_SF500		0x00010110

void flamingo28_decode(uint32_t raw, uint16_t *xmitter, uint8_t *command, uint8_t *channel, uint8_t *payload, uint8_t *rolling);
void flamingo32_decode(uint32_t raw, uint16_t *xmitter, uint8_t *command, uint8_t *channel, uint8_t *payload);

uint32_t flamingo28_encode(uint16_t xmitter, uint8_t channel, uint8_t command, uint8_t payload, uint8_t rolling);
uint32_t flamingo32_encode(uint16_t xmitter, uint8_t channel, uint8_t command, uint8_t payload);
uint32_t flamingo24_encode(uint16_t xmitter, uint8_t channel, uint8_t command, uint8_t payload);

void flamingo_send_FA500(int remote, uint8_t channel, uint8_t command, uint8_t rolling);
void flamingo_send_SF500(int remote, uint8_t channel, uint8_t command);
