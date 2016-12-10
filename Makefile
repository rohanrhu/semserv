C=gcc
CFLAGS=-c -Wall

all:
	$(C) -pthread -o build/semserv.exe src/semserv.c

clean:
	rm -rf build/*
