all: slides.dvi

slides.dvi: slides.tex
	csplain slides.tex

slides.ps: slides.dvi
	dvips -o slides.ps -D600 -ta4 slides.dvi

view: slides.dvi
	xdvi -expert -geometry 1100x700 -l -s 4 -paper a4 -margins 1in slides.dvi

viewlarge: slides.dvi
	xdvi -expert -geometry 1024x700 -l -s 1 -paper a4 -margins 1in slides.dvi

viewreal: slides.dvi
	xdvi -expert -geometry 1024x700 -l -s 6 -paper a4 -margins 1in slides.dvi

clean:
	rm -f *~ *.log *.tfm *.*pk *.*gf *.ps *.dvi
