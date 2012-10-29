CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall -Werror -O0 -g
LDFLAGS=-pthread -upthread_create

.PHONY: all
all: ajaxer ajaxers
.PHONY: clean
clean:
	rm -f ajaxer ajaxers *.o

ajaxer: ajaxer.o

ajaxers: ajaxers.o 

%.o: protocol.h

romfs:
	$(STRIP) ajaxer
	$(STRIP) ajaxers
	$(ROMFSINST) ajaxer /usr/local/bin/ajaxer
	$(ROMFSINST) ajaxers /usr/local/bin/ajaxers
