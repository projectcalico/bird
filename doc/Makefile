root-rel=../
dir-name=doc

include ../Rules

progdocs:
	mkdir -p prog
	$(srcdir)/tools/progdoc $(srcdir_abs)

%.html: %.sgml
	./sgml2html $<

%.info: %.sgml
	sgml2info $<

%.txt: %.sgml
	sgml2txt $<

%.dvi: %.sgml
	./sgml2latex $<

%.tex: %.sgml
	./sgml2latex --output=tex $<

userdocs: bird.html bird.dvi

clean:
	rm *.html *.tex *.dvi *.log
