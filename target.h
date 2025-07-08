//#define ANUS
#define TRON
//#define ODROID
//#define PIWOLF
//#define PICAM
//#define SABRE18
//#define SABRE28

#ifdef ANUS
#define MUSIC 			"/opt/music/"
#define DISPLAY			"/dev/tty"
#endif

#ifdef TRON
#define FLAMINGO
#define SOLAR
#define LCD
#define I2C				"/dev/i2c-7"
#define MIXER			"/usr/bin/amixer -q -D hw:CARD=USB2496play set PCM"
#endif

#ifdef ODROID
#define FLAMINGO
#define SOLAR
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

#ifdef SOLAR
//#define SOLAR_SIMULATOR
#define SOLAR_MODBUS
//#define SOLAR_API
#endif
