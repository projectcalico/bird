root-rel=../
dir-name=doc

progdocs:
	mkdir -p prog
	$(srcdir)/tools/progdoc $(srcdir_abs)

userdocs:

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

clean:
	rm *.html *.tex *.dvi *.log
