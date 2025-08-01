UNAME_M := $(shell uname -m)

INCLUDE = ./include
LIB = ./lib/$(UNAME_M)

CFLAGS = -I$(INCLUDE) -Wall
#CFLAGS = -I$(INCLUDE) -Wall -std=gnu99 -ggdb3 -Og
LIBS = -L$(LIB) -lpthread -lmpdclient -lFLAC -lid3tag -lmagic -lm

SRCS := $(shell find . -maxdepth 1 -name '*.c' | sort)
OBJS := $(patsubst %.c, %.o, $(SRCS))

COBJS-COMMON	= mcp.o frozen.o utils.o sensors.o i2c.o
COBJS-ANUS 		= $(COBJS-COMMON) mpd.o replaygain.o mp3gain-id3.o mp3gain-ape.o dac-alsa.o 
COBJS-TRON 		= $(COBJS-COMMON) mpd.o replaygain.o mp3gain-id3.o mp3gain-ape.o dac-alsa.o mqtt.o tasmota.o xmas.o ledstrip.o shutter.o flamingo.o solar.o sunspec.o mosmix.o aqua.o gpio-dummy.o curl.o button.o lcd.o
COBJS-ODROID 	= $(COBJS-COMMON) mqtt.o tasmota.o xmas.o ledstrip.o shutter.o flamingo.o solar.o sunspec.o mosmix.o aqua.o gpio-dummy.o curl.o
COBJS-PIWOLF 	= $(COBJS-COMMON) mpd.o replaygain.o mp3gain-id3.o mp3gain-ape.o dac-piwolf.o devinput-infrared.o gpio-bcm2835.o
COBJS-SABRE18 	= $(COBJS-COMMON) mpd.o replaygain.o mp3gain-id3.o mp3gain-ape.o dac-es9018.o devinput-infrared.o gpio-sunxi.o
COBJS-SABRE28 	= $(COBJS-COMMON) mpd.o replaygain.o mp3gain-id3.o mp3gain-ape.o dac-es9028.o devinput-infrared.o gpio-sunxi.o display.o display-menu.o devinput-rotary.o
COBJS-PICAM		= $(COBJS-COMMON) webcam.o xmas.o mqtt.o tasmota.o flamingo.o gpio-bcm2835.o

all: $(OBJS)
	@echo "detected $(UNAME_M) architecture"
	@echo "To create executables specify target: \"make (anus|tron|odroid|picam|piwolf|sabre18|sabre28)\""

#
# mcp main programs  
#

anus: CFLAGS += -DANUS
anus: clean $(COBJS-ANUS)
	$(CC) $(CFLAGS) -o mcp $(COBJS-ANUS) $(LIBS) -lmqttc

tron: CFLAGS += -DTRON
tron: clean $(COBJS-TRON)
	$(CC) $(CFLAGS) -o mcp $(COBJS-TRON) $(LIBS) -lmqttc -lmodbus -lcurl

odroid: CFLAGS += -DODROID
odroid: clean $(COBJS-ODROID) 
	$(CC) $(CFLAGS) -o mcp $(COBJS-ODROID) $(LIBS) -lmqttc -lmodbus -lcurl

picam: CFLAGS += -DPICAM
picam: clean $(COBJS-PICAM)
	$(CC) $(CFLAGS) -o mcp $(COBJS-PICAM) $(LIBS) -lmqttc

piwolf: CFLAGS += -DPIWOLF
piwolf: clean $(COBJS-PIWOLF)
	$(CC) $(CFLAGS) -o mcp $(COBJS-PIWOLF) $(LIBS)

sabre18: CFLAGS += -DSABRE18
sabre18: clean $(COBJS-SABRE18)
	$(CC) $(CFLAGS) -o mcp $(COBJS-SABRE18) $(LIBS)

sabre28: CFLAGS += -DSABRE28
sabre28: clean $(COBJS-SABRE28)
	$(CC) $(CFLAGS) -o mcp $(COBJS-SABRE28) $(LIBS) -lncurses -lmenu

#
# module tests
#
flamingo: flamingo.o utils.o gpio-bcm2835.o
	$(CC) $(CFLAGS) -DFLAMINGO_MAIN -c flamingo.c
	$(CC) $(CFLAGS) -o flamingo flamingo.o utils.o gpio-bcm2835.o

sensors: sensors.o utils.o i2c.o
	$(CC) $(CFLAGS) -DSENSORS_MAIN -c sensors.c
	$(CC) $(CFLAGS) -o sensors sensors.o utils.o i2c.o

solar: utils.o sensors.o i2c.o sunspec.o curl.o frozen.o
	$(CC) $(CFLAGS) -DSOLAR_MAIN -c solar.c mosmix.c
	$(CC) $(CFLAGS) -L$(LIB) -o solar solar.o mosmix.o utils.o sensors.o i2c.o sunspec.o curl.o frozen.o -lm -lmodbus -lcurl -lmqttc

simulator: clean utils.o sensors.o i2c.o sunspec.o curl.o frozen.o mqtt.o tasmota.o
	$(CC) $(CFLAGS) -DSOLAR_MAIN -DSIMULATOR -c solar.c mosmix.c
	$(CC) $(CFLAGS) -L$(LIB) -o simulator solar.o mosmix.o utils.o sensors.o i2c.o sunspec.o curl.o frozen.o mqtt.o tasmota.o -lm -lmodbus -lcurl -lmqttc

display: display.o display-menu.o utils.o i2c.o dac-es9028.o gpio-sunxi.o
	$(CC) $(CFLAGS) -DDISPLAY_MAIN -c display.c
	$(CC) $(CFLAGS) -o display display.o display-menu.o utils.o i2c.o dac-es9028.o gpio-sunxi.o -lncurses -lmenu -lm

gpio-sunxi: gpio-sunxi.o utils.o
	$(CC) $(CFLAGS) -DGPIO_MAIN -c gpio-sunxi.c
	$(CC) $(CFLAGS) -o gpio-sunxi gpio-sunxi.o utils.o

gpio-bcm2835: gpio-bcm2835.o utils.o
	$(CC) $(CFLAGS) -DGPIO_MAIN -c gpio-bcm2835.c
	$(CC) $(CFLAGS) -o gpio-bcm2835 gpio-bcm2835.o utils.o
	
switch: switch.o gpio-sunxi.o utils.o
	$(CC) $(CFLAGS) -c switch.c gpio-sunxi.c utils.c
	$(CC) $(CFLAGS) -o switch switch.o gpio-sunxi.o utils.o

template: template.o utils.o
	$(CC) $(CFLAGS) -DTEMPLATE_MAIN -c template.c
	$(CC) $(CFLAGS) -o template template.o utils.o

aqua: aqua.o utils.o
	$(CC) $(CFLAGS) -DAQUA_MAIN -c aqua.c
	$(CC) $(CFLAGS) -o aqua aqua.o utils.o

valgrind:
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=/tmp/valgrind-out.txt ./mcp
	
.PHONY: clean install install-local install-service keytable

clean:
	find . -type f -name '*.o' -delete
	rm -f mcp display switch sensors flamingo test gpio-sunxi gpio-bcm2835 solar simulator sunspec mosmix template aqua ledstrip

install:
	@echo "[Installing and starting mcp]"
	systemctl stop mcp
	install -m 0755 mcp /usr/local/bin
	systemctl start mcp

install-service:
	@echo "[Installing systemd service unit]"
	mkdir -p /usr/local/lib/systemd/system/
	install -m 0644 misc/mcp.service /usr/local/lib/systemd/system/
	install -m 0644 misc/universum /etc/rc_keymaps
	install -m 0644 misc/minidisc /etc/rc_keymaps
	install -m 0644 misc/90-devinput-infrared.rules /etc/udev/rules.d
	install -m 0644 misc/90-devinput-rotary.rules /etc/udev/rules.d
	systemctl daemon-reload
	systemctl enable mcp
	armbian-add-overlay misc/gpio-rotary.dts
	armbian-add-overlay misc/display-st7735r.dts

install-local:
	@echo "[Installing and starting local mcp]"
	killall -q mcp || true
	install -m 0755 mcp ~hje/bin
	~hje/bin/mcp

install-www:
	@echo "[Installing www pages]"
	cp -rv www/pv /server/www/
	cp -rv www/webcam /server/www/
	chown -R hje:hje /server/www/pv /server/www/webcam
	chown www-data:www-data /server/www/pv /server/www/webcam 
	chmod g+w /server/www/pv /server/www/webcam

keytable:
	@if [ ! -f /usr/include/linux/input-event-codes.h ]; then \
	  echo "Error you must set KERNEL_DIR to point to an extracted kernel source dir"; \
	  exit 1; \
	fi
	@echo generating keytable.h
	@printf "struct parse_event {\n\tchar *name;\n\tunsigned int value;\n};\n" > include/keytable.h

	@printf "struct parse_event events_type[] = {\n" >> include/keytable.h
	@more /usr/include/linux/input-event-codes.h | perl -n \
	-e 'if (m/^\#define\s+(EV_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2 if ($$1 ne "EV_VERSION"); }' \
	>> include/keytable.h
	@printf "\t{ NULL, 0}\n};\n" >> include/keytable.h

	@printf "struct parse_event msc_events[] = {\n" >> include/keytable.h
	@more /usr/include/linux/input-event-codes.h | perl -n \
	-e 'if (m/^\#define\s+(MSC_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2; }' \
	>> include/keytable.h
	@printf "\t{ NULL, 0}\n};\n" >> include/keytable.h

	@printf "struct parse_event key_events[] = {\n" >> include/keytable.h
	@more /usr/include/linux/input-event-codes.h | perl -n \
	-e 'if (m/^\#define\s+(KEY_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2; }' \
	-e 'if (m/^\#define\s+(BTN_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2; }' \
	>> include/keytable.h
	@printf "\t{ NULL, 0}\n};\n" >> include/keytable.h

	@printf "struct parse_event rel_events[] = {\n" >> include/keytable.h
	@more /usr/include/linux/input-event-codes.h | perl -n \
	-e 'if (m/^\#define\s+(REL_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2; }' \
	>> include/keytable.h
	@printf "\t{ NULL, 0}\n};\n" >> include/keytable.h

	@printf "struct parse_event abs_events[] = {\n" >> include/keytable.h
	@more /usr/include/linux/input-event-codes.h | perl -n \
	-e 'if (m/^\#define\s+(ABS_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2; }' \
	>> include/keytable.h
	@printf "\t{ NULL, 0}\n};\n" >> include/keytable.h
