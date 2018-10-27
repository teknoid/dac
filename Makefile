CFLAGS = -Wall

LIBS = -lpthread -lmpdclient -lFLAC -lid3tag -lmagic -lm

SRCS = $(wildcard *.c)
OBJS = $(SRCS:.c=.o)

COBJS = mcp.o devinput.o power.o mpdclient.o replaygain.o mp3gain.o utils.o
 
all: $(OBJS)
	@echo "To create executables specify target: anus | piwolf | sabre18 | sabre28"
 
anus: $(COBJS) dac-anus.o
	$(CC) $(CFLAGS) $(LIBS) -o mcp $(COBJS) dac-anus.o

piwolf: $(COBJS) dac-piwolf.o lirc.o
	$(CC) $(CFLAGS) $(LIBS) -lwiringPi -o mcp $(COBJS) dac-piwolf.o lirc.o

sabre18: $(COBJS) dac-es9018.o
	$(CC) $(CFLAGS) $(LIBS) -lwiringPi -o mcp $(COBJS) dac-es9018.o

sabre28: $(COBJS) dac-es9028.o rotary.o
	$(CC) $(CFLAGS) $(LIBS) -lwiringPi -o mcp $(COBJS) dac-es9028.o rotary.o

.c.o:
	$(CC) -c $(CFLAGS) $< 

.PHONY: clean

clean:
	rm -f *.o mcp test

install:
	killall -q mcp || true
	rm /usr/local/bin/mcp || true
	cp mcp /usr/local/bin
	/usr/local/bin/mcp

install-local:
	killall -q mcp || true
	rm ~hje/bin/mcp
	cp mcp ~hje/bin
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
