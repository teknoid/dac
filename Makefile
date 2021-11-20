CFLAGS = -Wall

LIBS = -lpthread -lmpdclient -lFLAC -lid3tag -lmagic -lm

SRCS := $(shell find . -name '*.c')
OBJS := $(patsubst %.c, %.o, $(SRCS))

COBJS-COMMON	= mcp.o mpdclient.o replaygain.o mp3gain.o utils.o 
COBJS-ANUS 		= $(COBJS-COMMON) dac-anus.o display.o
COBJS-PIWOLF 	= $(COBJS-COMMON) dac-piwolf.o devinput-infrared.o
COBJS-SABRE18 	= $(COBJS-COMMON) dac-es9018.o devinput-infrared.o
COBJS-SABRE28 	= $(COBJS-COMMON) dac-es9028.o i2c.o display.o display-menu.o devinput-infrared.o devinput-rotary.o

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ 

all: $(OBJS)
	@echo "To create executables specify target: anus | piwolf | sabre18 | sabre28"
 
anus: $(COBJS-ANUS) 
	$(CC) $(CFLAGS) -o mcp $(COBJS-ANUS) $(LIBS) -lncurses

piwolf: $(COBJS-PIWOLF)
	$(CC) $(CFLAGS) -o mcp $(COBJS-PIWOLF) $(LIBS) -lwiringPi

sabre18: $(COBJS-SABRE18)
	$(CC) $(CFLAGS) -o mcp $(COBJS-SABRE18) $(LIBS) -lwiringPi -lncurses -lmenu

sabre28: $(COBJS-SABRE28)
	$(CC) $(CFLAGS) -o mcp $(COBJS-SABRE28) $(LIBS) -lwiringPi -lncurses -lmenu

display: display.o display-menu.o utils.o i2c.o dac-es9028.o
	$(CC) $(CFLAGS) $(LIBS) -o display display.o display-menu.o utils.o i2c.o dac-es9028.o -lwiringPi -lncurses -lmenu

rotary2uinput: rotary2uinput.o
	$(CC) $(CFLAGS) $(LIBS) -o rotary2uinput rotary2uinput.c

.PHONY: clean install install-local install-service keytable

clean:
	find . -type f -name '*.o' -delete
	rm -f mcp display rotary2uinput test

install:
	@echo "[Installing and starting mcp]"
	systemctl stop mcp
	install -m 0755 mcp /usr/local/bin
	systemctl start mcp

install-service:
	@echo "[Installing systemd service unit]"
	mkdir -p /usr/local/lib/systemd/system/
	install -m 0644 misc/mcp.service /usr/local/lib/systemd/system/
	systemctl daemon-reload
	systemctl enable mcp

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
