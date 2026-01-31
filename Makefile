CC=gcc
CFLAGS=-Wall -Wextra -O2 -pthread

all: server client

server: server.c
	$(CC) $(CFLAGS) server.c -o server -lrt

client: client.c
	$(CC) $(CFLAGS) client.c -o client -lrt

clean:
	rm -f server client
	# IPC artifacts
	rm -rf /tmp/yahtzee
	rm -f /dev/shm/yahtzee_shm /dev/shm/sem.*

