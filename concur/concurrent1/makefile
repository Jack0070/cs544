#cs575 makefile by Li Li
#Compiling openmp codes with 'make'
#Compiling pthread codes with 'make pthread'
#Drawing plot with 'make plot' 
#Making latex with 'make pdf'

CC = gcc
CXX = g++
#-lm added in case we include math lib
CFLAGS = -Wall -std=c99 
CXXFLAGS = -Wall -std=c99 -lm -fopenmp
#when want to play with pthread add this
LDFLAGS = -pthread 


TARGET = concurrent1

#any headers go here
INCLUDES = 

#any .c or .cpp files go here
SOURCE = ${TARGET}.c

#My Latex file.
LATEXTARGET = ${TARGET}.tex

#default is to compile
default: pthread 

#depends on all of you source and header files
openmp: ${SOURCE} ${INCLUDES}
		${CC} -o ${TARGET} ${SOURCE} ${CFLAGS}

pthread: ${SOURCE} ${INCLUDES}
		${CC} -o ${TARGET} ${SOURCE} ${CFLAGS} ${LDFLAGS}
	
shell:
	rm ./result.txt -f
	printf '' > result.txt
	foreach dn (2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384)
		foreach cn (1 2 4 8 16 32)
			./${TARGET} $dn $cn >> result.txt
		end
	end
	

pdf: 
	pdflatex ${LATEXTARGET}.tex
	rm ${LATEXTARGET}.aux ${LATEXTARGET}.dvi ${LATEXTARGET}.log -f
	rm *~ -f
	rm ${LATEXTARGET}.pdf -f

plot:
	rm ./cache.dat
	./cache > cache.dat
	gnuplot result.gp
	
tar:
	rm CS444_${TARGET}_group26.tar.bz2
	tar -cvf CS444_${TARGET}_group26.tar.bz2 ${TARGET}.c makefile 

