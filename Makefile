UNAME_M := $(shell uname -m)

INCLUDE = ./include
LIB = ./lib/$(UNAME_M)

CFLAGS = -I$(INCLUDE) -Wall
LIBS = -L$(LIB) -lpthread -lmpdclient -lFLAC -lid3tag -lmagic -lm

SRCS := $(shell find . -maxdepth 1 -name '*.c')
OBJS := $(patsubst %.c, %.o, $(SRCS))

COBJS-COMMON	= mcp.o frozen.o utils.o
COBJS-ANUS 		= $(COBJS-COMMON) dac-anus.o mpd.o replaygain.o mp3gain.o
COBJS-TRON 		= $(COBJS-COMMON) dac-tron.o mpd.o replaygain.o mp3gain.o button.o lcd.o i2c.o mqtt.o tasmota.o xmas.o shutter.o flamingo.o gpio-dummy.o
COBJS-PIWOLF 	= $(COBJS-COMMON) dac-piwolf.o mpd.o replaygain.o mp3gain.o devinput-infrared.o gpio-bcm2835.o
COBJS-SABRE18 	= $(COBJS-COMMON) dac-es9018.o mpd.o replaygain.o mp3gain.o devinput-infrared.o gpio-sunxi.o
COBJS-SABRE28 	= $(COBJS-COMMON) dac-es9028.o mpd.o replaygain.o mp3gain.o devinput-infrared.o gpio-sunxi.o i2c.o display.o display-menu.o devinput-rotary.o
COBJS-PICAM		= $(COBJS-COMMON) webcam.o xmas.o sensors.o flamingo.o gpio-bcm2835.o i2c.o mqtt.o tasmota.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ 

all: $(OBJS)
	@echo "detected $(UNAME_M) architecture"
	@echo "To create executables specify target: \"make (tron|anus|piwolf|sabre18|sabre28|picam)\""
 
anus: $(COBJS-ANUS) 
	$(CC) $(CFLAGS) -o mcp $(COBJS-ANUS) $(LIBS) -lncurses -lmqttc

tron: $(COBJS-TRON) 
	$(CC) $(CFLAGS) -o mcp $(COBJS-TRON) $(LIBS) -lncurses -lmqttc

piwolf: $(COBJS-PIWOLF)
	$(CC) $(CFLAGS) -o mcp $(COBJS-PIWOLF) $(LIBS)

sabre18: $(COBJS-SABRE18)
	$(CC) $(CFLAGS) -o mcp $(COBJS-SABRE18) $(LIBS)

sabre28: $(COBJS-SABRE28)
	$(CC) $(CFLAGS) -o mcp $(COBJS-SABRE28) $(LIBS) -lncurses -lmenu

picam: $(COBJS-PICAM)
	$(CC) $(CFLAGS) -o mcp $(COBJS-PICAM) $(LIBS) -lmqttc

sensors: sensors.o utils.o i2c.o
	$(CC) $(CFLAGS) -DSENSORS_MAIN -c sensors.c
	$(CC) $(CFLAGS) -o sensors sensors.o utils.o i2c.o $(LIBS)

display: display.o display-menu.o utils.o i2c.o dac-es9028.o
	$(CC) $(CFLAGS) $(LIBS) -o display display.o display-menu.o utils.o i2c.o dac-es9028.o -lncurses -lmenu

rotary2uinput: rotary2uinput.o
	$(CC) $(CFLAGS) $(LIBS) -o rotary2uinput rotary2uinput.c

gpio-sunxi: gpio-sunxi.o
	$(CC) $(CFLAGS) -DGPIO_MAIN -c gpio-sunxi.c
	$(CC) $(CFLAGS) -o gpio-sunxi gpio-sunxi.o

gpio-bcm2835: gpio-bcm2835.o
	$(CC) $(CFLAGS) -DGPIO_MAIN -c gpio-bcm2835.c
	$(CC) $(CFLAGS) -o gpio-bcm2835 gpio-bcm2835.o
	
switch: switch.o gpio-sunxi.o utils.o
	$(CC) $(CFLAGS) -c switch.c
	$(CC) $(CFLAGS) -o switch switch.o gpio-sunxi.o utils.o

flamingo: flamingo.o utils.o
	$(CC) $(CFLAGS) -DLOCALMAIN -c flamingo.c
	$(CC) $(CFLAGS) -o flamingo flamingo.o utils.o

.PHONY: clean install install-local install-service keytable

clean:
	find . -type f -name '*.o' -delete
	rm -f mcp display rotary2uinput test gpio-sunxi gpio-bcm2835 switch

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

keytable:
	@if [ ! -f /usr/include/linux/input.h ]; then \
	  echo "Error you must set KERNEL_DIR to point to an extracted kernel source dir"; \
	  exit 1; \
	fi
	@echo generating keytable.h
	@printf "struct parse_event {\n\tchar *name;\n\tunsigned int value;\n};\n" > keytable.h

	@printf "struct parse_event events_type[] = {\n" >> keytable.h
	@more /usr/include/linux/input.h | perl -n \
	-e 'if (m/^\#define\s+(EV_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2 if ($$1 ne "EV_VERSION"); }' \
	>> keytable.h
	@printf "\t{ NULL, 0}\n};\n" >> keytable.h

	@printf "struct parse_event msc_events[] = {\n" >> keytable.h
	@more /usr/include/linux/input.h | perl -n \
	-e 'if (m/^\#define\s+(MSC_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2; }' \
	>> keytable.h
	@printf "\t{ NULL, 0}\n};\n" >> keytable.h

	@printf "struct parse_event key_events[] = {\n" >> keytable.h
	@more /usr/include/linux/input.h | perl -n \
	-e 'if (m/^\#define\s+(KEY_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2; }' \
	-e 'if (m/^\#define\s+(BTN_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2; }' \
	>> keytable.h
	@printf "\t{ NULL, 0}\n};\n" >> keytable.h

	@printf "struct parse_event rel_events[] = {\n" >> keytable.h
	@more /usr/include/linux/input.h | perl -n \
	-e 'if (m/^\#define\s+(REL_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2; }' \
	>> keytable.h
	@printf "\t{ NULL, 0}\n};\n" >> keytable.h

	@printf "struct parse_event abs_events[] = {\n" >> keytable.h
	@more /usr/include/linux/input.h | perl -n \
	-e 'if (m/^\#define\s+(ABS_[^\s]+)\s+(0x[\d\w]+|[\d]+)/) ' \
	-e '{ printf "\t{\"%s\", %s},\n",$$1,$$2; }' \
	>> keytable.h
	@printf "\t{ NULL, 0}\n};\n" >> keytable.h
