CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall -Werror

all: ajaxer ajaxers

ajaxer: ajaxer.o

ajaxers: ajaxers.o
