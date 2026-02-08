Yahtzee Multiplayer on Single-Machine

This project implements a local multiplayer Yahtzee game where multiple players
run separate client processes on the same machine. All communication is handled
through POSIX named pipes (FIFOs) and shared memory. The server coordinates all
turns, scoring, and overall game flow.

------------------------------------------------------------
1. HOW TO COMPILE
------------------------------------------------------------

To compile all components, run:

    make

This produces two executables:
    server
    client

To remove binaries and IPC artifacts:

    make clean


------------------------------------------------------------
2. HOW TO RUN (EXAMPLE COMMANDS)
------------------------------------------------------------

Step 1: Start the server (Terminal 1)

    ./server

The server will initialize the IPC directory and create the main FIFO:
    /tmp/yahtzee/server_fifo


Step 2: Start the clients (Terminal 2, Terminal 3, ...)

Run one client for each player:

    ./client

Each client creates its own FIFOs under /tmp/yahtzee/, based on its PID:
    client_<pid>
    client_<pid>_read

Follow the displayed prompts to enter your name and play the game.


------------------------------------------------------------
3. GAME RULES SUMMARY
------------------------------------------------------------

The game follows standard Yahtzee rules:

- Each player takes turns rolling dice.
- Players may reroll according to server prompts.
- Players select a scoring category for each round.
- Each scoring category is typically used once.
- The game continues until all rounds are completed.
- The player with the highest total score wins.

The server also keeps a persistent record of total wins stored in scores.txt,
allowing win counts to accumulate across multiple sessions.


------------------------------------------------------------
4. MODES SUPPORTED
------------------------------------------------------------

Supported Mode:
Single-machine multiplayer using:
    * POSIX named pipes (FIFOs)
    * Shared memory
    * Semaphores / synchronization
    * Multiple server threads/processes

------------------------------------------------------------
TROUBLESHOOTING
------------------------------------------------------------

If IPC files remain or cause errors, run:

    make clean

If the server crashes, leftover shared memory or semaphore objects may appear
in /dev/shm/. Cleaning usually removes the common leftovers.

