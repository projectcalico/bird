# Makefile for the BIRD Internet Routing Daemon
# (c) 1998 Martin Mares <mj@ucw.cz>

TOPDIR=$(shell pwd)
OBJDIR=obj

CPPFLAGS=-I$(TOPDIR)/$(OBJDIR) -I$(TOPDIR)
OPT=-O2
DEBUG=-g#gdb
CFLAGS=$(OPT) $(DEBUG) -Wall -W -Wstrict-prototypes -Wno-unused -Wno-parentheses

PROTOCOLS=
LIBDIRS=sysdep/linux sysdep/unix lib
STDDIRS=nest $(addprefix proto/,$(PROTOCOLS))
DIRS=$(STDDIRS) $(OBJDIR)/lib
PARTOBJS=$(join $(addsuffix /,$(STDDIRS)),$(subst /,_,$(addsuffix .o,$(STDDIRS))))
LIBS=$(OBJDIR)/lib/birdlib.a

export

all: .dep all-dirs bird

all-dirs:
	set -e ; for a in $(DIRS) ; do $(MAKE) -C $$a this ; done

bird: $(PARTOBJS) $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $^

.dep:
	$(MAKE) dep

dep:
	mkdir -p $(OBJDIR)
	tools/mergedirs $(OBJDIR) $(LIBDIRS)
#	for a in $(STDDIRS) ; do mkdir -p $(OBJDIR)/$$a ; done
	set -e ; for a in $(DIRS) ; do $(MAKE) -C $$a dep ; done
	touch .dep

clean:
	rm -rf obj
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name "\#*\#" -or -name TAGS -or -name core -or -name .depend -or -name .#*`
	rm -f bird .dep
