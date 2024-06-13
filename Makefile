BIN = bench
CC = gcc
CFLAGS = -Wall -O2
LDLIBS = -luv -lm

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
	CFLAGS += -I$(shell brew --prefix libuv)/include
	LDFLAGS += -L$(shell brew --prefix libuv)/lib
endif

all: $(BIN)

$(BIN):

.PHONY: clean
clean:
	rm -f './bench'
