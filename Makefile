LIBS=-lwiringPi -lpthread -lmpdclient -lFLAC -lid3tag -lmagic -lm
#LIBS=-lpthread -lmpdclient -lFLAC -lid3tag -lmagic -lm
#CFLAGS=-I/usr/include/libmpd-1.0/libmpd/ -Wall -Wmissing-prototypes -Wmissing-declarations
CFLAGS=-Wall -Wmissing-prototypes -Wmissing-declarations
CC = gcc

all: mcp

mcp: mcp.o devinput.o lirc.o dac.o power.o mpdclient.o replaygain.o mp3gain.o utils.o
	$(CC) $(CFLAGS) $(LIBS) -o mcp mcp.o devinput.o lirc.o dac.o power.o mpdclient.o replaygain.o mp3gain.o utils.o

test: test.o mp3gain.o
	$(CC) $(CFLAGS) $(LIBS) -o test test.o mp3gain.o 

.PHONY: clean

clean:
	rm -f *.o mcp test

install:
	killall -q mcp || true
	cp ~hje/workspace-cpp/dac/mcp /usr/local/bin
	/usr/local/bin/mcp

install-local:
	killall -q mcp || true
	cp ~hje/workspace-cpp/dac/mcp ~hje/bin
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
