CC = gcc
CFLAGS = -pthread -Wall -Wextra -g
LIBS = -lrt -lpthread

# Targets
all: server client

# Server compilation
server: server.c
	$(CC) $(CFLAGS) -o server server.c $(LIBS)
	@echo "✓ Server compiled successfully"

# Client compilation
client: client.c
	$(CC) $(CFLAGS) -o client client.c
	@echo "✓ Client compiled successfully"

# Clean build files and shared memory
clean:
	rm -f server client
	rm -f /dev/shm/yahtzee_shm
	rm -f game_log.txt scores.txt
	@echo "✓ Cleaned build files and shared memory"

# Run server
run-server: server
	./server

# Run client (can specify IP: make run-client IP=192.168.1.100)
IP ?= 127.0.0.1
run-client: client
	./client $(IP)

# Help
help:
	@echo "Yahtzee Multiplayer Game - Makefile"
	@echo "===================================="
	@echo "Targets:"
	@echo "  make          - Compile both server and client"
	@echo "  make server   - Compile server only"
	@echo "  make client   - Compile client only"
	@echo "  make clean    - Remove binaries and shared memory"
	@echo "  make run-server - Compile and run server"
	@echo "  make run-client - Compile and run client"
	@echo ""
	@echo "Usage:"
	@echo "  Terminal 1: make run-server"
	@echo "  Terminal 2: make run-client"
	@echo "  Terminal 3: make run-client"
	@echo "  Terminal 4: make run-client"

.PHONY: all clean run-server run-client help
