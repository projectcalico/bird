# Makefile for the BIRD Internet Routing Daemon
# (c) 1998 Martin Mares <mj@ucw.cz>

TOPDIR=$(shell pwd)
CPPFLAGS=-I$(TOPDIR)
CFLAGS=-O2 -Wall -W -Wstrict-prototypes -Wno-unused -Wno-parentheses $(CPPFLAGS)

PROTOCOLS=
DIRS=sysdep/linux nest $(PROTOCOLS) lib
ARCHS=$(join $(addsuffix /,$(DIRS)),$(subst /,_,$(addsuffix .a,$(DIRS))))

export

all: .dep all-dirs bird

all-dirs:
	set -e ; for a in $(DIRS) ; do $(MAKE) -C $$a ; done

bird: $(ARCHS)
	$(CC) $(LDFLAGS) -o $@ $^

.dep:
	$(MAKE) dep
	touch .dep

dep:
	set -e ; for a in $(DIRS) ; do $(MAKE) -C $$a dep ; done

clean:
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name "\#*\#" -or -name TAGS -or -name core -or -name .depend`
	rm -f bird .dep
