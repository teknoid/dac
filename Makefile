INCLUDE = ./include
LIB = ./lib

CFLAGS = -I$(INCLUDE) -Wall
LIBS = -L$(LIB) -lpthread -lmpdclient -lFLAC -lid3tag -lmagic -lm -lmqttc

SRCS := $(shell find . -maxdepth 1 -name '*.c')
OBJS := $(patsubst %.c, %.o, $(SRCS))

COBJS-COMMON	= mcp.o mpdclient.o replaygain.o mp3gain.o utils.o
COBJS-ANUS 		= $(COBJS-COMMON) dac-anus.o display.o
COBJS-TRON 		= $(COBJS-COMMON) dac-tron.o button.o lcd.o i2c.o mqtt.o frozen.o
COBJS-PIWOLF 	= $(COBJS-COMMON) dac-piwolf.o devinput-infrared.o gpio-bcm2835.o
COBJS-SABRE18 	= $(COBJS-COMMON) dac-es9018.o devinput-infrared.o gpio-sunxi.o
COBJS-SABRE28 	= $(COBJS-COMMON) dac-es9028.o devinput-infrared.o gpio-sunxi.o i2c.o display.o display-menu.o devinput-rotary.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ 

all: $(OBJS)
	@echo "To create executables specify target: \"make (tron|anus|piwolf|sabre18|sabre28)\""
 
anus: $(COBJS-ANUS) 
	$(CC) $(CFLAGS) -o mcp $(COBJS-ANUS) $(LIBS) -lncurses

tron: $(COBJS-TRON) 
	$(CC) $(CFLAGS) -o mcp $(COBJS-TRON) $(LIBS) -lncurses

piwolf: $(COBJS-PIWOLF)
	$(CC) $(CFLAGS) -o mcp $(COBJS-PIWOLF) $(LIBS)

sabre18: $(COBJS-SABRE18)
	$(CC) $(CFLAGS) -o mcp $(COBJS-SABRE18) $(LIBS)

sabre28: $(COBJS-SABRE28)
	$(CC) $(CFLAGS) -o mcp $(COBJS-SABRE28) $(LIBS) -lncurses -lmenu

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
