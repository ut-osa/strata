.PHONY: clean

CFLAGS  := -Wall -g
LD      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := master slave

all: ${APPS}

master: common.o master.o
	${LD} -o $@ $^ ${LDLIBS}

slave: common.o slave.o
	${LD} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}



