C=gcc
CFLAGS=-c -Wall

all:
	mkdir -p build
	$(C) -pthread -o build/semserv src/semserv.c
	@echo "\n\texecutable created: build/semserv\n"

clean:
	rm -rf build/*
