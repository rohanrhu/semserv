C=gcc
CFLAGS=-c -Wall

all:
	$(C) -o build/semserv.exe src/semserv.c

clean:
	rm -rf build/*