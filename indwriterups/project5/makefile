#CS311 LATEX write-up makefile
#by Li Li

latexfile = project5

default: pdf

pdf: 
	pdflatex ${latexfile}.tex
 
	
 
	rm ${latexfile}.aux ${latexfile}.dvi ${latexfile}.log -f
	rm *~ -f
tar:
	tar -jcvf CS411_${latexfile}_writeup.tar.bz2 makefile ${latexfile}.pdf ${latexfile}.tex




