# Makefile for the BIRD Internet Routing Daemon
# (c) 1998 Martin Mares <mj@ucw.cz>

TOPDIR=$(shell pwd)
CPPFLAGS=-I$(TOPDIR)/sysdep/linux -I$(TOPDIR)
OPT=-O2
DEBUG=-g#gdb
CFLAGS=$(OPT) $(DEBUG) -Wall -W -Wstrict-prototypes -Wno-unused -Wno-parentheses

PROTOCOLS=
DIRS=nest $(PROTOCOLS) lib sysdep/linux sysdep/unix
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
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name "\#*\#" -or -name TAGS -or -name core -or -name .depend -or -name .#*`
	rm -f bird .dep
