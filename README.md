Yahtzee Server — Logger & Persistence (Member 4)

Overview
- This project implements a Yahtzee server with IPC-based local multiplayer.
- Member 4 responsibilities implemented here:
  - Logger thread with a bounded log queue (`LOG_QUEUE_SIZE`) and semaphores.
  - Persistence of `total_wins` per player to `scores.txt` with `flock` locking.
  - Small documentation and test evidence.

Files of interest
- server.c : Main server with logger and persistence added.
- scores.txt : Created/updated by the server to persist total wins.
- Makefile : Build targets (`make` builds `server` and `client`).

How it works
1. On startup, the server initializes shared memory and logger semaphores, then
   calls `load_scores_from_file()` to restore `total_wins` for known player names.
2. A background `logger_thread` consumes messages from a bounded queue and
   writes them to stdout.
3. When a game finishes, `finalize_game_nolock()` increments the winner's
   `total_wins` and calls `save_scores_to_file()` which writes `scores.txt`
   under an exclusive `flock`.

Quick start
Build:

```sh
make
```

Run server (foreground):

```sh
./server
```

Notes
- `scores.txt` lines are in the format `PlayerName:Wins`.
- The logger queue is process-shared and uses semaphores so children can enqueue
  messages safely.

