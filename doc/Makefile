root-rel=../
dir-name=doc

ifneq ($(wildcard ../Rules),)
include ../Rules
else
srcdir=$(shell cd $(root-rel) ; pwd)
srcdir_abs=$(srcdir)
endif

# Force rebuilds
.PHONY: prog.sgml bird.sgml

docs: progdocs userdocs

progdocs: prog.html

prog.sgml:
	$(srcdir)/tools/progdoc $(srcdir_abs)

%.html: %.sgml
	./sgml2html $<

%.info: %.sgml
	sgml2info $<

%.dvi: %.sgml
	./sgml2latex $<

%.tex: %.sgml
	./sgml2latex --output=tex $<

%.txt: %.sgml
	./sgml2txt $<

userdocs: bird.html bird.dvi bird.txt

clean:
	rm -f *.html *.tex *.dvi *.log *.ps *.txt
	rm -f prog.sgml
