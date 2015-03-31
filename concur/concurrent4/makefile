TARGET = concurrent4

default: barbershop

barbershop: barbershop.c
	gcc -o barbershop -Wall -std=c99 -pthread barbershop.c
tar:
	tar -cvf CS444_${TARGET}_group26.tar.bz2 *.c makefile