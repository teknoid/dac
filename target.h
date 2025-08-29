// --> set as compiler directive in Makefile
//#define ANUS
//#define TRON
//#define ODROID
//#define PICAM
//#define PIWOLF
//#define SABRE18
//#define SABRE28

#ifdef ANUS
#endif

#ifdef TRON
#define FLAMINGO
#define LCD
#define I2C				"/dev/i2c-7"
#define MIXER			"/usr/bin/amixer -q -D hw:CARD=USB2496play set PCM"
#endif

#ifdef ODROID
#define FLAMINGO
#endif

#ifdef PICAM
// /boot/firmware/config.txt: dtparam=i2c_vc=on
#define I2C				"/dev/i2c-0"
#endif

#ifdef PIWOLF
#define DEVINPUT_IR		"/dev/input/infrared"
#endif

#ifdef SABRE18
#define DAC
#define DEVINPUT_IR		"/dev/input/infrared"
#endif

#ifdef SABRE28
#define DAC
#define DEVINPUT_IR		"/dev/input/infrared"
#define DEVINPUT_RA		"/dev/input/rotary_axis"
#define DEVINPUT_RB		"/dev/input/rotary_button"
#define DISPLAY			"/dev/tty1"
#define I2C				"/dev/i2c-0"
#endif
