#define SPACEMASK_FA500		0x01000110
#define SPACEMASK_SF500		0x00010110

const static uint16_t REMOTES[5] = { 0x53cc, 0x835a, 0x31e2, 0x295c, 0x272d };

void flamingo28_decode(unsigned int raw, unsigned int *xmitter, unsigned char *command, unsigned char *channel, unsigned char *payload, unsigned char *rolling);
void flamingo32_decode(unsigned int raw, unsigned int *xmitter, unsigned char *command, unsigned char *channel, unsigned char *payload);

unsigned int flamingo28_encode(unsigned int xmitter, char channel, char command, char payload, char rolling);
unsigned int flamingo32_encode(unsigned int xmitter, char channel, char command, char payload);
