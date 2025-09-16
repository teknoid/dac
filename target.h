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
#define SOLAR
#define FLAMINGO
#define LCD
#define I2C						"/dev/i2c-7"
#define MIXER					"/usr/bin/amixer -q -D hw:CARD=USB2496play set PCM"
#define TEMP_IN					(tasmota_get_by_id(DEVKIT1) ? tasmota_get_by_id(DEVKIT1)->htu21_temp : 0)
#define TEMP_OUT				(tasmota_get_by_id(CARPORT) ? tasmota_get_by_id(CARPORT)->sht31_temp : 0)
#define HUMI					(tasmota_get_by_id(CARPORT) ? tasmota_get_by_id(CARPORT)->sht31_humi : 0)
#define LUMI					(tasmota_get_by_id(CARPORT) ? tasmota_get_by_id(CARPORT)->bh1750_lux : 0)
#define SUNDOWN					25
#define SUNRISE					50
#endif

#ifdef ODROID
#define FLAMINGO
#endif

#ifdef PICAM
// /boot/firmware/config.txt: dtparam=i2c_vc=on
#define I2C						"/dev/i2c-0"
#define TEMP_IN					sensors->bmp085_temp
#define TEMP_OUT				sensors->bmp085_temp
#define LUMI					sensors->bh1750_lux
#endif

#ifdef PIWOLF
#define DEVINPUT_IR				"/dev/input/infrared"
#endif

#ifdef SABRE18
#define DAC
#define DEVINPUT_IR				"/dev/input/infrared"
#endif

#ifdef SABRE28
#define DAC
#define DEVINPUT_IR				"/dev/input/infrared"
#define DEVINPUT_RA				"/dev/input/rotary_axis"
#define DEVINPUT_RB				"/dev/input/rotary_button"
#define DISPLAY					"/dev/tty1"
#define I2C						"/dev/i2c-0"
#endif
