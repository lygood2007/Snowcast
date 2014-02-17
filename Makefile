CC = gcc
DEBUGFLAGS = -g -Wall
CFLAGS = -D_REENTRANT $(DEBUGFLAGS) -D_XOPEN_SOURCE=500
LDFLAGS = -lpthread 

all: snowcast_listener snowcast_control snowcast_server

snowcast_listener: snowcast_listener.c
snowcast_control:  snowcast_control.c
snowcast_server:   snowcast_server.c
clean:
	rm -f snowcast_listener snowcast_control snowcast_server
