# Makefile for the BIRD Internet Routing Daemon
# (c) 1998 Martin Mares <mj@ucw.cz>

TOPDIR=$(shell pwd)
CFLAGS=-O2 -Wall -W -Wstrict-prototypes -Wno-unused -Wno-parentheses -I$(TOPDIR)

PROTOCOLS=
DIRS=sysdep/linux nest $(protocols) lib
ARCHS=$(join $(addsuffix /,$(DIRS)),$(subst /,_,$(addsuffix .a,$(DIRS))))

export

all: all-dirs bird

all-dirs:
	set -e ; for a in $(DIRS) ; do $(MAKE) -C $$a ; done

bird: $(ARCHS)
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name "\#*\#" -or -name TAGS -or -name core`
	rm -f bird
