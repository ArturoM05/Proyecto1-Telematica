CC      = gcc
CFLAGS  = -Wall -Wextra -g -O2
LDFLAGS = -lpthread

all: pibl server

pibl: pibl.c pibl.h
	$(CC) $(CFLAGS) -o pibl pibl.c $(LDFLAGS)

server: tws.c
	$(CC) $(CFLAGS) -o server tws.c $(LDFLAGS)

clean:
	rm -f pibl server *.o

.PHONY: all clean
