# Generated automatically from Makefile-top.in by configure.
# Makefile for in place build of BIRD
# (c) 1999 Martin Mares <mj@ucw.cz>

objdir=obj

all:
	$(MAKE) -C $(objdir) $@

clean:
	$(MAKE) -C $(objdir) clean
	rm -f `find . -name "*~" -or -name "*.[oa]" -or -name "\#*\#" -or -name TAGS -or -name core -or -name depend -or -name .#*`

distclean: clean
	rm -rf $(objdir)
	rm -f config.* configure sysdep/autoconf.h Makefile
