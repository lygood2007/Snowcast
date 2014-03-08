CC = gcc
DEBUGFLAGS = -g -Wall
CFLAGS = -D_REENTRANT $(DEBUGFLAGS) -D_XOPEN_SOURCE=500
LDFLAGS = -lpthread 

all: snowcast_listener snowcast_control snowcast_server

snowcast_listener: snowcast_listener.c

snowcast_control: snowcast_control.o command_queue.o
	gcc -g -Wall -o snowcast_control snowcast_control.o command_queue.o

snowcast_server: snowcast_server.c

snowcast_control.o: snowcast_control.c
	gcc -g -Wall -o snowcast_control.o -c snowcast_control.c

command_queue.o: command_queue.c
	gcc -g -Wall -o command_queue.o -c command_queue.c

clean:
	rm -f snowcast_listener snowcast_control snowcast_server *.o
