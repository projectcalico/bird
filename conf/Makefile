CONFS=$(TOPDIR)/conf/confbase.Y @CONFS@$(addprefix $(TOPDIR)/,$(addsuffix /config.Y,$(BASEDIRS)))
OBJS=cf-parse.tab.o cf-lex.o
CONF=$(TOPDIR)/conf

ifdef DEBUG
BISON_DEBUG=-t
#FLEX_DEBUG=-d
endif

include $(TOPDIR)/Rules

cf-parse.tab.c cf-parse-tab.h: cf-parse.y
	bison -bcf-parse -dv -pcf_ $(BISON_DEBUG) cf-parse.y

cf-parse.y: $(CONFS) $(CONF)/gen_parser.m4
	m4 -P $(CONF)/gen_parser.m4 $(CONFS) >cf-parse.y

keywords.h: $(CONFS) $(CONF)/gen_keywords.m4
	m4 -P $(CONF)/gen_keywords.m4 $(CONFS) >keywords.h

cf-lex.c: cf-lex.l
	flex $(FLEX_DEBUG) -sB8 -ocf-lex.c -Pcf_ cf-lex.l

dep: keywords.h
