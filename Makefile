CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall -Werror
LDFLAGS=-pthread -upthread_create

all: ajaxer ajaxers

ajaxer: ajaxer.o

ajaxers: ajaxers.o 

%.o: protocol.h

