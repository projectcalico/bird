# Makefile for bird documentation system


SHELL = /bin/sh

all: bird.html

clean:
	rm *.html *.tex *.dvi *.log

%.html: %.sgml
	./sgml2html $<

dnl INFO format:

%.info: %.sgml
	sgml2info $<

dnl TXT format:

%.txt: %.sgml
	sgml2txt $<

dnl LATEX format:

%.dvi: %.sgml
	./sgml2latex $<

%.tex: %.sgml
	./sgml2latex --output=tex $<
