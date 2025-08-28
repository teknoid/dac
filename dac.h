#include <mpd/status.h>

#ifndef BUFSIZE
#define BUFSIZE			256
#endif

typedef enum {
	nlock, dsd, pcm, spdif, dop
} dac_signal_t;

typedef enum {
	mpd, opt, coax
} dac_source_t;

typedef struct dac_state_t {
	dac_signal_t dac_signal;
	dac_source_t dac_source;
	int state;
	int dac_state_changed;
	int ext_power;
	int dac_power;
	int dac_rate;
	int dac_volume;
	int dac_mute;
	enum mpd_state mpd_state;
	int mpd_bits;
	int mpd_rate;
	int clock_h;
	int clock_m;
	int nightmode;
	double load;
	double temp;
	int plist_key;
	int plist_pos;
	char artist[BUFSIZE];
	char title[BUFSIZE];
	char album[BUFSIZE];
	char extension[8];
	int menu;
	int ir_active;
	int switch1;
	int switch2;
	int switch3;
	int switch4;
	int notifications_lcd;
	int notifications_sound;
	int notifications_desktop;
} dac_state_t;
extern dac_state_t *dac;

void dac_power(void);
void dac_mute(void);
void dac_unmute(void);
void dac_volume_up(void);
void dac_volume_down(void);
void dac_source(int);
void dac_handle(int);
