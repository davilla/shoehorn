#
# if you want the loader in a special location (e.g. to RPMize shoehorn) use
#	make all [ EXTRAFLAGS="-DLOADERPATH=/usr/lib/shoehorn/" ]
# if you have sudo use
#	make suid [ SUDO=sudo ]
# select an install path with
#	make install INSTALLPREFIX=/usr (or /usr/local)
#

CROSS := arm-linux-
CFLAGS ?= -g -Wall
override CFLAGS += $(EXTRAFLAGS)
INSTALL := install
INSTALLPREFIX ?= /usr/local
LDFLAGS := -g

WHOAMI := $(shell whoami)
ifneq ($(WHOAMI),root)
	SUDO := sudo
endif

SRCS := eth.c serial.c shoehorn.c util.c
OBJS := $(SRCS:.c=.o)
DEPS := $(SRCS:.c=.d)

# The shoehorn loader needs to be setuid root to use packet sockets
# (needed for Ethernet download).  We only need this for the machine
# which actually has the EDB7211 connected to it.
#

all: loader.bin shoehorn

suid: .setuid.stamp loader.bin

install: all
	$(INSTALL) -c -m 4755 -o root -g root shoehorn $(INSTALLPREFIX)/bin/shoehorn
	$(INSTALL) -c -m 644 -o root -g root loader.bin $(INSTALLPREFIX)/lib/shoehorn/loader.bin

.setuid.stamp: shoehorn
	$(SUDO) chown root shoehorn
	$(SUDO) chmod u+s,go-w shoehorn
	touch .setuid.stamp

shoehorn: $(OBJS)
	rm -f .setuid.stamp
	$(CC) $(LDFLAGS) -o $@ $^

loader.elf: init.S loader.c cs8900.h ep7211.h ioregs.h
	$(CROSS)gcc -Wall -fomit-frame-pointer -Os -nostdlib \
		-Wl,-Ttext,0x10000000 -N init.S loader.c -o loader.elf

%.bin: %.elf
	$(CROSS)objcopy -O binary $^ $@

# automated dependency checking
include $(DEPS)

%.d: %.c
	$(CC) $(CFLAGS) -M $< > $@

# housecleaning
.PHONY: clean scrub
clean:
	rm -f shoehorn core
	rm -f loader.elf loader.bin loader.s
	rm -f *.o
scrub: clean
	rm -f .setuid.stamp
	rm -f $(DEPS)

