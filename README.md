Yahtzee Server

Overview

* This project implements a Yahtzee server with IPC-based local multiplayer.
* Member 4 responsibilities implemented here:

  * Logger thread with a bounded log queue ('LOG_QUEUE_SIZE') and semaphores.
  * Persistence of 'total_wins' per player to 'scores.txt' with 'flock' locking.
  * Documentation and test evidence.

Files of interest

* server.c : Main server with logger and persistence added.
* client.c : Named pipe client used to play the game.
* scores.txt : Created/updated by the server to persist total wins.
* Makefile : Build targets ('make' builds 'server' and 'client').

How it works

1. On startup, the server initializes shared memory and logger semaphores, then calls 'load_scores_from_file()' to restore 'total_wins'.
2. A background 'logger_thread' consumes messages from a bounded queue and writes them to stdout.
3. When a game finishes, 'finalize_game_nolock()' increments the winner's 'total_wins' and calls 'save_scores_to_file()' which writes 'scores.txt' under exclusive 'flock' locking.

How to Compile and Run
Build everything:

make

Run server:

./server

Run clients (in separate terminals):

./client
./client
./client

Clean build artifacts:

make clean

Example Commands
Terminal 1:
./server

Terminal 2:
./client

Terminal 3:
./client

Players then follow prompts to join and play.

Game Rules Summary

* Each player takes turns rolling five dice.
* Players may reroll selected dice up to two times per turn.
* A scoring category must be chosen each round.
* Standard Yahtzee scoring rules apply (upper and lower sections).
* After all rounds, the player with the highest score wins.
* Total wins are persisted across sessions.

Supported Mode

* Local single-machine multiplayer
* Communication via POSIX Named Pipes (FIFOs)
* Multi-process server handling multiple clients
* Round-robin turn scheduling with timeout enforcement

Notes

* 'scores.txt' lines are in the format 'PlayerName:Wins'
* Logger queue is process-shared so child processes can enqueue safely
