#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>

// Configuration
#define MAX_PLAYERS 5
#define MAX_ROUNDS 13
#define NAME_SIZE 50
#define BUFFER_SIZE 2048
#define FIFO_DIR "/tmp/yahtzee"
#define SERVER_FIFO "/tmp/yahtzee/server_fifo"

#define QUANTUM_SECONDS 90

// Shared Memory Structure
typedef struct {
    int current_turn;
    int active_players;
    int target_players;
    int host_player_id;
    int game_started;
    int game_round;
    int game_finished;

    // NEW: Freeze who participates in the match at game start
    int participants[MAX_PLAYERS];      // 1 if this player is in THIS match
    int participants_count;             // should == target_players at start

    // NEW: endgame bookkeeping
    int player_done[MAX_PLAYERS];       // 1 if filled 13 categories (or forfeited)
    int final_scores[MAX_PLAYERS];
    int winner_id;

    char player_names[MAX_PLAYERS][NAME_SIZE];
    int  player_connected[MAX_PLAYERS];

    int  player_dice[MAX_PLAYERS][5];
    int  player_rerolls_left[MAX_PLAYERS];

    int  player_scores[MAX_PLAYERS][15][3];

    char yahtzee_achieved[MAX_PLAYERS];
    int  amount_yahtzee[MAX_PLAYERS];
    int  required_upper_section[MAX_PLAYERS];
    char lower_section_only[MAX_PLAYERS];
    char skip_scoring[MAX_PLAYERS];
    char bonus_achieved[MAX_PLAYERS];
    char upper_section_filled[MAX_PLAYERS];
    char lower_section_filled[MAX_PLAYERS];

    struct timespec turn_deadline[MAX_PLAYERS];

    pthread_mutex_t game_mutex;
    pthread_mutex_t log_mutex;
    sem_t turn_sem[MAX_PLAYERS];
    sem_t turn_done_sem[MAX_PLAYERS];
    int  turn_active[MAX_PLAYERS];

    int total_wins[MAX_PLAYERS];
} GameState;

GameState *game_state;

static void update_section_flags_nolock(int player_id);
static int  maybe_award_upper_bonus_nolock(int player_id);

// ------------------------------ Helpers ------------------------------

int compare_int(const void *a, const void *b) {
    return (*(int*)a - *(int*)b);
}

int has_n_of_a_kind(int dice[], int n) {
    for (int num = 1; num <= 6; num++) {
        int count = 0;
        for (int i = 0; i < 5; i++) {
            if (dice[i] == num) count++;
        }
        if (count >= n) return 1;
    }
    return 0;
}

int is_full_house(int dice[]) {
    int counts[7] = {0};
    for (int i = 0; i < 5; i++) counts[dice[i]]++;
    int has_three = 0, has_two = 0;
    for (int i = 1; i <= 6; i++) {
        if (counts[i] == 3) has_three = 1;
        if (counts[i] == 2) has_two = 1;
    }
    return has_three && has_two;
}

int has_small_straight(int dice[]) {
    int present[7] = {0};
    for (int i = 0; i < 5; i++) present[dice[i]] = 1;
    return (present[1] && present[2] && present[3] && present[4]) ||
           (present[2] && present[3] && present[4] && present[5]) ||
           (present[3] && present[4] && present[5] && present[6]);
}

int has_large_straight(int dice[]) {
    int present[7] = {0};
    for (int i = 0; i < 5; i++) present[dice[i]] = 1;
    return (present[1] && present[2] && present[3] && present[4] && present[5]) ||
           (present[2] && present[3] && present[4] && present[5] && present[6]);
}

static int timespec_cmp(const struct timespec *a, const struct timespec *b) {
    if (a->tv_sec != b->tv_sec) return (a->tv_sec > b->tv_sec) ? 1 : -1;
    if (a->tv_nsec != b->tv_nsec) return (a->tv_nsec > b->tv_nsec) ? 1 : -1;
    return 0;
}

static int ms_until_deadline(const struct timespec *deadline) {
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    if (timespec_cmp(&now, deadline) >= 0) return 0;

    long sec = (long)(deadline->tv_sec - now.tv_sec);
    long nsec = (long)(deadline->tv_nsec - now.tv_nsec);
    if (nsec < 0) { nsec += 1000000000L; sec -= 1; }

    long ms = sec * 1000L + nsec / 1000000L;
    if (ms < 0) ms = 0;
    if (ms > 2147483000L) ms = 2147483000L;
    return (int)ms;
}

// reads up to (sz-1) bytes, returns:
//  >0 bytes read
//   0 EOF
//  -1 error
//  -2 turn timed out
static int timed_read_line(int fd, char *buf, size_t sz, const struct timespec *deadline) {
    int timeout_ms = ms_until_deadline(deadline);
    if (timeout_ms <= 0) return -2;

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    int pr = poll(&pfd, 1, timeout_ms);
    if (pr == 0) return -2;
    if (pr < 0) return -1;

    int n = (int)read(fd, buf, sz - 1);
    if (n > 0) buf[n] = '\0';
    return n;
}

// ------------------------------ Endgame logic (NEW) ------------------------------

static int player_finished_nolock(int pid) {
    int scored = 0;
    for (int c = 0; c < 13; c++) {
        if (game_state->player_scores[pid][c][1] == 1) scored++;
    }
    return (scored == 13);
}

static void wake_all_players_nolock(void) {
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (game_state->participants[p]) {
            sem_post(&game_state->turn_sem[p]); // release any sem_wait stuck clients
        }
    }
}

static void finalize_game_nolock(void) {
    if (game_state->game_finished) return;

    int best = -1;
    int best_score = -1;

    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (!game_state->participants[p]) continue;

        // Ensure bonus is correct before totaling
        maybe_award_upper_bonus_nolock(p);

        int total = 0;
        for (int i = 0; i < 15; i++) total += game_state->player_scores[p][i][0];

        game_state->final_scores[p] = total;

        if (total > best_score) { best_score = total; best = p; }
    }

    game_state->winner_id = best;
    game_state->game_finished = 1;

    if (best >= 0) game_state->total_wins[best] += 1;

    wake_all_players_nolock();
}

static void maybe_end_game_nolock(void) {
    if (!game_state->game_started) return;
    int need = game_state->participants_count;
    if (need <= 0) return;

    int done = 0;
    for (int p = 0; p < MAX_PLAYERS; p++) {
        if (!game_state->participants[p]) continue;

        if (player_finished_nolock(p)) game_state->player_done[p] = 1;
        if (game_state->player_done[p]) done++;
    }

    if (done == need) finalize_game_nolock();
}

// Auto-forfeit remaining categories on disconnect (NEW safety)
static void forfeit_remaining_on_disconnect_nolock(int player_id) {
    if (!game_state->participants[player_id]) return;
    if (game_state->player_done[player_id]) return;

    for (int cat = 0; cat < 13; cat++) {
        if (game_state->player_scores[player_id][cat][1] == 0) {
            game_state->player_scores[player_id][cat][0] = 0;
            game_state->player_scores[player_id][cat][1] = 1;
        }
    }
    update_section_flags_nolock(player_id);
    maybe_award_upper_bonus_nolock(player_id);

    game_state->player_done[player_id] = 1;
    maybe_end_game_nolock();
}

// ------------------------------ Timeout scoring ------------------------------

static int apply_zero_next_available_nolock(int player_id) {
    for (int cat = 0; cat < 13; cat++) {
        if (game_state->player_scores[player_id][cat][1] == 0) {
            game_state->player_scores[player_id][cat][0] = 0;
            game_state->player_scores[player_id][cat][1] = 1;
            update_section_flags_nolock(player_id);
            maybe_award_upper_bonus_nolock(player_id);
            // NEW: after applying a score, check end condition
            maybe_end_game_nolock();
            return cat;
        }
    }
    return -1;
}

static void forfeit_turn_timeout(int player_id, int write_fd) {
    while (sem_trywait(&game_state->turn_done_sem[player_id]) == 0) {
    }

    pthread_mutex_lock(&game_state->game_mutex);
    int cat = apply_zero_next_available_nolock(player_id);
    pthread_mutex_unlock(&game_state->game_mutex);

    if (cat >= 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "\n[TIMEOUT] 90 seconds expired. You forfeit this turn.\n"
                 "Auto-scored 0 in your next available category (category #%d).\n"
                 "Turn ended.\n\n", cat + 1);
        write(write_fd, msg, strlen(msg));
    } else {
        const char *msg =
            "\n[TIMEOUT] 90 seconds expired. No categories left to score.\n"
            "Turn ended.\n\n";
        write(write_fd, msg, strlen(msg));
    }

    sem_post(&game_state->turn_done_sem[player_id]);
}

// ------------------------------ Game logic ------------------------------

void roll_dice(int player_id) {
    pthread_mutex_lock(&game_state->game_mutex);
    for (int i = 0; i < 5; i++) {
        game_state->player_dice[player_id][i] = rand() % 6 + 1;
    }
    pthread_mutex_unlock(&game_state->game_mutex);
    printf("[GAME] Player %d rolled dice\n", player_id + 1);
}

void reroll_dice(int player_id, int dice_to_reroll[], int count) {
    pthread_mutex_lock(&game_state->game_mutex);
    for (int i = 0; i < count; i++) {
        int idx = dice_to_reroll[i] - 1;
        if (idx >= 0 && idx < 5) {
            game_state->player_dice[player_id][idx] = rand() % 6 + 1;
        }
    }
    pthread_mutex_unlock(&game_state->game_mutex);
}

void calculate_possible_scores(int player_id) {
    int dice[5];

    pthread_mutex_lock(&game_state->game_mutex);

    for (int i = 0; i < 5; i++) dice[i] = game_state->player_dice[player_id][i];
    qsort(dice, 5, sizeof(int), compare_int);

    for (int i = 0; i < 15; i++) game_state->player_scores[player_id][i][2] = 0;

    for (int i = 0; i < 5; i++) {
        if (dice[i] == 1) game_state->player_scores[player_id][0][2] += 1;
        if (dice[i] == 2) game_state->player_scores[player_id][1][2] += 2;
        if (dice[i] == 3) game_state->player_scores[player_id][2][2] += 3;
        if (dice[i] == 4) game_state->player_scores[player_id][3][2] += 4;
        if (dice[i] == 5) game_state->player_scores[player_id][4][2] += 5;
        if (dice[i] == 6) game_state->player_scores[player_id][5][2] += 6;
    }

    if (has_n_of_a_kind(dice, 3)) {
        int sum = 0;
        for (int i = 0; i < 5; i++) sum += dice[i];
        game_state->player_scores[player_id][6][2] = sum;
    }

    if (has_n_of_a_kind(dice, 4)) {
        int sum = 0;
        for (int i = 0; i < 5; i++) sum += dice[i];
        game_state->player_scores[player_id][7][2] = sum;
    }

    if (is_full_house(dice)) game_state->player_scores[player_id][8][2] = 25;
    if (has_small_straight(dice)) game_state->player_scores[player_id][9][2] = 30;
    if (has_large_straight(dice)) game_state->player_scores[player_id][10][2] = 40;

    if (has_n_of_a_kind(dice, 5)) {
        game_state->player_scores[player_id][11][2] = 50;
        game_state->required_upper_section[player_id] = dice[0] - 1; // 0..5
    }

    int sum = 0;
    for (int i = 0; i < 5; i++) sum += dice[i];
    game_state->player_scores[player_id][12][2] = sum;

    if (game_state->yahtzee_achieved[player_id] == 'Y' &&
        game_state->player_scores[player_id][11][2] == 50) {
        game_state->player_scores[player_id][8][2]  = 25;
        game_state->player_scores[player_id][9][2]  = 30;
        game_state->player_scores[player_id][10][2] = 40;
    }

    pthread_mutex_unlock(&game_state->game_mutex);
}

static void update_section_flags_nolock(int player_id) {
    int c = 0;
    for (int i = 0; i < 6; i++) if (game_state->player_scores[player_id][i][1] == 1) c++;
    game_state->upper_section_filled[player_id] = (c == 6) ? 'Y' : 'N';

    c = 0;
    for (int i = 6; i <= 12; i++) if (game_state->player_scores[player_id][i][1] == 1) c++;
    game_state->lower_section_filled[player_id] = (c == 7) ? 'Y' : 'N';
}

static int maybe_award_upper_bonus_nolock(int player_id) {
    if (game_state->bonus_achieved[player_id] == 'Y') return 0;

    int upper_total = 0;
    for (int i = 0; i < 6; i++) upper_total += game_state->player_scores[player_id][i][0];

    if (upper_total >= 63) {
        game_state->player_scores[player_id][13][0] = 35;
        game_state->player_scores[player_id][13][1] = 1;
        game_state->bonus_achieved[player_id] = 'Y';
        return 1;
    }
    return 0;
}

int apply_score(int player_id, int category) {
    pthread_mutex_lock(&game_state->game_mutex);

    if (category < 0 || category >= 13 || game_state->player_scores[player_id][category][1] == 1) {
        pthread_mutex_unlock(&game_state->game_mutex);
        return 0;
    }

    game_state->player_scores[player_id][category][0] = game_state->player_scores[player_id][category][2];
    game_state->player_scores[player_id][category][1] = 1;

    if (category == 11 && game_state->player_scores[player_id][category][0] == 50) {
        game_state->yahtzee_achieved[player_id] = 'Y';
    }

    update_section_flags_nolock(player_id);
    maybe_award_upper_bonus_nolock(player_id);

    // NEW: check endgame right after scoring
    maybe_end_game_nolock();

    pthread_mutex_unlock(&game_state->game_mutex);
    return 1;
}

int calculate_total_score(int player_id) {
    int total = 0;

    pthread_mutex_lock(&game_state->game_mutex);
    maybe_award_upper_bonus_nolock(player_id);
    for (int i = 0; i < 15; i++) total += game_state->player_scores[player_id][i][0];
    pthread_mutex_unlock(&game_state->game_mutex);

    return total;
}

// ------------------------------ Shared Memory Init ------------------------------

int init_shared_memory() {
    shm_unlink("/yahtzee_shm");

    int shm_fd = shm_open("/yahtzee_shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return -1;
    }

    if (ftruncate(shm_fd, sizeof(GameState)) == -1) {
        perror("ftruncate failed");
        close(shm_fd);
        return -1;
    }

    game_state = (GameState*) mmap(NULL, sizeof(GameState),
                                   PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (game_state == MAP_FAILED) {
        perror("mmap failed");
        close(shm_fd);
        return -1;
    }

    memset(game_state, 0, sizeof(GameState));

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);

    pthread_mutex_init(&game_state->game_mutex, &mutex_attr);
    pthread_mutex_init(&game_state->log_mutex, &mutex_attr);

    pthread_mutexattr_destroy(&mutex_attr);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        sem_init(&game_state->turn_sem[i], 1, 0);
        sem_init(&game_state->turn_done_sem[i], 1, 0);
        game_state->turn_active[i] = 0;
        game_state->turn_deadline[i].tv_sec = 0;
        game_state->turn_deadline[i].tv_nsec = 0;
    }

    game_state->current_turn   = 0;
    game_state->active_players = 0;
    game_state->target_players = 0;
    game_state->host_player_id = -1;
    game_state->game_started   = 0;
    game_state->game_round     = 1;
    game_state->game_finished  = 0;

    // NEW init
    game_state->participants_count = 0;
    game_state->winner_id = -1;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        game_state->participants[i] = 0;
        game_state->player_done[i] = 0;
        game_state->final_scores[i] = 0;
    }

    for (int i = 0; i < MAX_PLAYERS; i++) {
        game_state->player_connected[i] = 0;
        game_state->total_wins[i] = 0;

        game_state->yahtzee_achieved[i] = 'N';
        game_state->amount_yahtzee[i] = 0;
        game_state->required_upper_section[i] = -1;
        game_state->lower_section_only[i] = 'N';
        game_state->skip_scoring[i] = 'N';
        game_state->bonus_achieved[i] = 'N';
        game_state->upper_section_filled[i] = 'N';
        game_state->lower_section_filled[i] = 'N';

        memset(game_state->player_names[i], 0, NAME_SIZE);

        for (int j = 0; j < 15; j++) {
            for (int k = 0; k < 3; k++) {
                game_state->player_scores[i][j][k] = 0;
            }
        }
    }

    printf("✓ Shared memory initialized (fresh)\n");
    return 0;
}

// ------------------------------ SIGCHLD ------------------------------

void sigchld_handler(int sig) {
    (void)sig;
    int saved_errno = errno;
    pid_t pid;
    while ((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        printf("[SYSTEM] Child process %d reaped\n", pid);
    }
    errno = saved_errno;
}

void setup_signal_handlers() {
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction failed");
        exit(1);
    }
    printf("✓ Signal handlers set up\n");
}

// ------------------------------ IPC ------------------------------

int setup_ipc_server() {
    struct stat st = {0};
    if (stat(FIFO_DIR, &st) == -1) {
        if (mkdir(FIFO_DIR, 0777) == -1) {
            perror("mkdir failed");
            return -1;
        }
    }

    unlink(SERVER_FIFO);
    if (mkfifo(SERVER_FIFO, 0666) == -1) {
        perror("mkfifo failed");
        return -1;
    }

    printf("✓ IPC setup complete (Named Pipes)\n");
    return 0;
}

static void reject_client(const char* client_fifo, const char* msg) {
    char client_read_fifo[256];
    snprintf(client_read_fifo, sizeof(client_read_fifo), "%s_read", client_fifo);

    int wfd = open(client_fifo, O_WRONLY);
    int rfd = open(client_read_fifo, O_RDONLY);

    if (wfd >= 0) {
        write(wfd, msg, strlen(msg));
        close(wfd);
    }
    if (rfd >= 0) close(rfd);
}

// ------------------------------ Client Handler ------------------------------

void handle_client(int player_id, const char* client_fifo) {
    char buffer[BUFFER_SIZE];
    char recv_buffer[256];
    char client_read_fifo[256];

    snprintf(client_read_fifo, sizeof(client_read_fifo), "%s_read", client_fifo);

    int write_fd = open(client_fifo, O_WRONLY);
    int read_fd  = open(client_read_fifo, O_RDONLY);

    if (write_fd < 0 || read_fd < 0) {
        perror("open FIFOs failed");
        exit(1);
    }

    snprintf(buffer, sizeof(buffer), "Enter your name: ");
    write(write_fd, buffer, strlen(buffer));

    int n = read(read_fd, recv_buffer, sizeof(recv_buffer) - 1);
    if (n > 0) {
        recv_buffer[n] = '\0';
        recv_buffer[strcspn(recv_buffer, "\n")] = '\0';
        pthread_mutex_lock(&game_state->game_mutex);
        strncpy(game_state->player_names[player_id], recv_buffer, NAME_SIZE - 1);
        pthread_mutex_unlock(&game_state->game_mutex);
    }

    snprintf(buffer, sizeof(buffer), "Welcome %s! You are Player %d\n",
             game_state->player_names[player_id], player_id + 1);
    write(write_fd, buffer, strlen(buffer));

    pthread_mutex_lock(&game_state->game_mutex);
    if (game_state->host_player_id < 0) game_state->host_player_id = player_id;
    int host_id = game_state->host_player_id;
    pthread_mutex_unlock(&game_state->game_mutex);

    if (player_id == host_id) {
        while (1) {
            pthread_mutex_lock(&game_state->game_mutex);
            int target = game_state->target_players;
            int connected = game_state->active_players;
            pthread_mutex_unlock(&game_state->game_mutex);

            if (target > 0) break;

            snprintf(buffer, sizeof(buffer),
                     "\n[HOST SETUP] Enter number of players for this game (3-%d): ",
                     MAX_PLAYERS);
            write(write_fd, buffer, strlen(buffer));

            n = read(read_fd, recv_buffer, sizeof(recv_buffer) - 1);
            if (n <= 0) {
                close(write_fd);
                close(read_fd);
                exit(0);
            }
            recv_buffer[n] = '\0';
            int t = atoi(recv_buffer);

            if (t >= 3 && t <= MAX_PLAYERS) {
                pthread_mutex_lock(&game_state->game_mutex);
                game_state->target_players = t;
                pthread_mutex_unlock(&game_state->game_mutex);

                snprintf(buffer, sizeof(buffer),
                         "✓ Lobby set to %d players. Currently connected: %d/%d\n"
                         "Waiting for remaining players to join...\n",
                         t, connected, t);
                write(write_fd, buffer, strlen(buffer));
                break;
            } else {
                snprintf(buffer, sizeof(buffer),
                         "Invalid number. Please enter a value between 3 and %d.\n",
                         MAX_PLAYERS);
                write(write_fd, buffer, strlen(buffer));
            }
        }
    } else {
        snprintf(buffer, sizeof(buffer),
                 "Waiting for host to choose number of players...\n");
        write(write_fd, buffer, strlen(buffer));

        while (1) {
            pthread_mutex_lock(&game_state->game_mutex);
            int target = game_state->target_players;
            int connected = game_state->active_players;
            pthread_mutex_unlock(&game_state->game_mutex);

            if (target > 0) {
                snprintf(buffer, sizeof(buffer),
                         "Host selected %d players. Currently connected: %d/%d\n",
                         target, connected, target);
                write(write_fd, buffer, strlen(buffer));
                break;
            }
            sleep(1);
        }
    }

    snprintf(buffer, sizeof(buffer), "Waiting for game to start...\n");
    write(write_fd, buffer, strlen(buffer));

    while (1) {
        pthread_mutex_lock(&game_state->game_mutex);
        int started = game_state->game_started;
        pthread_mutex_unlock(&game_state->game_mutex);

        if (started) break;
        sleep(1);
    }

    // If this player isn't a participant (shouldn't happen), exit.
    pthread_mutex_lock(&game_state->game_mutex);
    int am_participant = game_state->participants[player_id];
    pthread_mutex_unlock(&game_state->game_mutex);
    if (!am_participant) {
        write(write_fd, "Server: You are not a participant in this match.\n", 51);
        close(write_fd);
        close(read_fd);
        exit(0);
    }

    snprintf(buffer, sizeof(buffer), "\n*** GAME STARTING! ***\n\n");
    write(write_fd, buffer, strlen(buffer));

    const char *categories[] = {
        "Aces", "Twos", "Threes", "Fours", "Fives", "Sixes",
        "Three of a Kind", "Four of a Kind", "Full House",
        "Small Straight", "Large Straight", "Yahtzee", "Chance"
    };

    // NEW: real loop (not fake round counter)
    while (1) {
        sem_wait(&game_state->turn_sem[player_id]);

        pthread_mutex_lock(&game_state->game_mutex);
        int finished = game_state->game_finished;
        int my_done  = game_state->player_done[player_id];
        struct timespec deadline = game_state->turn_deadline[player_id];
        pthread_mutex_unlock(&game_state->game_mutex);

        if (finished || my_done) break;
        if (!game_state->player_connected[player_id]) break;

        snprintf(buffer, sizeof(buffer),
                 "\n========================================\n"
                 "[YOUR TURN, %s]\n"
                 "========================================\n",
                 game_state->player_names[player_id]);
        write(write_fd, buffer, strlen(buffer));

        pthread_mutex_lock(&game_state->game_mutex);
        game_state->player_rerolls_left[player_id] = 2;
        pthread_mutex_unlock(&game_state->game_mutex);

        roll_dice(player_id);

        snprintf(buffer, sizeof(buffer), "Your dice: [%d] [%d] [%d] [%d] [%d]\n",
                 game_state->player_dice[player_id][0],
                 game_state->player_dice[player_id][1],
                 game_state->player_dice[player_id][2],
                 game_state->player_dice[player_id][3],
                 game_state->player_dice[player_id][4]);
        write(write_fd, buffer, strlen(buffer));

        // Reroll
        while (game_state->player_rerolls_left[player_id] > 0) {
            snprintf(buffer, sizeof(buffer), "\nRerolls left: %d. Reroll? (Y/N): ",
                     game_state->player_rerolls_left[player_id]);
            write(write_fd, buffer, strlen(buffer));

            n = timed_read_line(read_fd, recv_buffer, sizeof(recv_buffer), &deadline);
            if (n == -2) { forfeit_turn_timeout(player_id, write_fd); goto next_turn; }
            if (n <= 0) break;

            if (recv_buffer[0] == 'N' || recv_buffer[0] == 'n') break;

            if (recv_buffer[0] == 'Y' || recv_buffer[0] == 'y') {
                snprintf(buffer, sizeof(buffer), "Which dice? (e.g., 1 3 5): ");
                write(write_fd, buffer, strlen(buffer));

                n = timed_read_line(read_fd, recv_buffer, sizeof(recv_buffer), &deadline);
                if (n == -2) { forfeit_turn_timeout(player_id, write_fd); goto next_turn; }
                if (n <= 0) break;

                int dice_to_reroll[5];
                int count = 0;
                char *token = strtok(recv_buffer, " \n");
                while (token && count < 5) {
                    int die = atoi(token);
                    if (die >= 1 && die <= 5) dice_to_reroll[count++] = die;
                    token = strtok(NULL, " \n");
                }

                if (count > 0) {
                    reroll_dice(player_id, dice_to_reroll, count);
                    pthread_mutex_lock(&game_state->game_mutex);
                    game_state->player_rerolls_left[player_id]--;
                    pthread_mutex_unlock(&game_state->game_mutex);

                    snprintf(buffer, sizeof(buffer), "New dice: [%d] [%d] [%d] [%d] [%d]\n",
                             game_state->player_dice[player_id][0],
                             game_state->player_dice[player_id][1],
                             game_state->player_dice[player_id][2],
                             game_state->player_dice[player_id][3],
                             game_state->player_dice[player_id][4]);
                    write(write_fd, buffer, strlen(buffer));
                }
            }
        }

        calculate_possible_scores(player_id);

        // Yahtzee extra/Joker/forced rules
        pthread_mutex_lock(&game_state->game_mutex);

        game_state->skip_scoring[player_id] = 'N';
        game_state->lower_section_only[player_id] = 'N';

        int rolled_yahtzee = (game_state->player_scores[player_id][11][2] == 50);

        if (rolled_yahtzee) {
            if (game_state->amount_yahtzee[player_id] >= 1) {
                snprintf(buffer, sizeof(buffer),
                         "\n\nCongratulations! You scored another Yahtzee!\n");
                write(write_fd, buffer, strlen(buffer));

                game_state->amount_yahtzee[player_id] += 1;

                if (game_state->yahtzee_achieved[player_id] == 'Y') {
                    game_state->player_scores[player_id][14][0] += 100;
                    game_state->player_scores[player_id][14][1] = 1;
                    snprintf(buffer, sizeof(buffer),
                             "Yahtzee bonus awarded! (+100)\n");
                    write(write_fd, buffer, strlen(buffer));
                }

                if (game_state->player_scores[player_id][11][1] == 1 &&
                    game_state->yahtzee_achieved[player_id] == 'Y') {

                    int req = game_state->required_upper_section[player_id]; // 0..5
                    if (req >= 0 && req < 6 && game_state->player_scores[player_id][req][1] == 0) {
                        // Auto-score required upper section
                        game_state->player_scores[player_id][req][0] =
                            game_state->player_scores[player_id][req][2];
                        game_state->player_scores[player_id][req][1] = 1;

                        snprintf(buffer, sizeof(buffer),
                                 "Since you scored another Yahtzee and UPPER SECTION #%d is available,\n"
                                 "it has been automatically filled with %d points.\n",
                                 req + 1, game_state->player_scores[player_id][req][0]);
                        write(write_fd, buffer, strlen(buffer));

                        game_state->skip_scoring[player_id] = 'Y';

                        // NEW: end-check after auto-fill
                        update_section_flags_nolock(player_id);
                        maybe_award_upper_bonus_nolock(player_id);
                        maybe_end_game_nolock();

                    } else if (req >= 0 && req < 6 &&
                               game_state->player_scores[player_id][req][1] == 1 &&
                               game_state->lower_section_filled[player_id] == 'N') {
                        snprintf(buffer, sizeof(buffer),
                                 "Since UPPER SECTION #%d is NOT available, you may use this Yahtzee\n"
                                 "to score any LOWER SECTION category.\n",
                                 req + 1);
                        write(write_fd, buffer, strlen(buffer));
                        game_state->lower_section_only[player_id] = 'Y';
                    }
                }
            } else {
                snprintf(buffer, sizeof(buffer),
                         "\n\nCongratulations! You scored a Yahtzee!\n");
                write(write_fd, buffer, strlen(buffer));
                game_state->amount_yahtzee[player_id] = 1;
            }
        }

        update_section_flags_nolock(player_id);
        maybe_award_upper_bonus_nolock(player_id);

        pthread_mutex_unlock(&game_state->game_mutex);

        // Scoring selection (skip if server auto-scored required upper box)
        if (game_state->skip_scoring[player_id] == 'N') {
            snprintf(buffer, sizeof(buffer), "\n=== SCORING OPTIONS ===\n");
            write(write_fd, buffer, strlen(buffer));

            if (game_state->lower_section_only[player_id] == 'N') {
                for (int i = 0; i < 13; i++) {
                    if (game_state->player_scores[player_id][i][1] == 0) {
                        snprintf(buffer, sizeof(buffer), "%2d. %-20s | %d points\n",
                                 i + 1, categories[i],
                                 game_state->player_scores[player_id][i][2]);
                        write(write_fd, buffer, strlen(buffer));
                    }
                }
            } else {
                for (int i = 6; i < 13; i++) {
                    if (game_state->player_scores[player_id][i][1] == 0) {
                        snprintf(buffer, sizeof(buffer), "%2d. %-20s | %d points\n",
                                 i + 1, categories[i],
                                 game_state->player_scores[player_id][i][2]);
                        write(write_fd, buffer, strlen(buffer));
                    }
                }
            }

            int valid = 0, choice = -1;
            while (!valid) {
                if (game_state->lower_section_only[player_id] == 'N') {
                    snprintf(buffer, sizeof(buffer), "\nChoose category (1-13): ");
                } else {
                    snprintf(buffer, sizeof(buffer), "\nChoose LOWER category (7-13): ");
                }
                write(write_fd, buffer, strlen(buffer));

                n = timed_read_line(read_fd, recv_buffer, sizeof(recv_buffer), &deadline);
                if (n == -2) { forfeit_turn_timeout(player_id, write_fd); goto next_turn; }
                if (n <= 0) break;
                recv_buffer[n] = '\0';

                choice = atoi(recv_buffer);

                if (game_state->lower_section_only[player_id] == 'Y' && choice < 7) {
                    valid = 0;
                } else if (choice >= 1 && choice <= 13 &&
                           game_state->player_scores[player_id][choice - 1][1] == 0) {
                    valid = 1;
                } else {
                    snprintf(buffer, sizeof(buffer), "Invalid choice! Try again.\n");
                    write(write_fd, buffer, strlen(buffer));
                }
            }

            if (valid && apply_score(player_id, choice - 1)) {
                snprintf(buffer, sizeof(buffer), "Scored %d points in %s!\n",
                         game_state->player_scores[player_id][choice - 1][0],
                         categories[choice - 1]);
                write(write_fd, buffer, strlen(buffer));
            }
        }

        // If player just finished, mark done (redundant but safe)
        pthread_mutex_lock(&game_state->game_mutex);
        if (player_finished_nolock(player_id)) game_state->player_done[player_id] = 1;
        maybe_end_game_nolock();
        pthread_mutex_unlock(&game_state->game_mutex);

        // Show current scorecard
        pthread_mutex_lock(&game_state->game_mutex);

        snprintf(buffer, sizeof(buffer), "\nCurrent Score:\nUpper Section\n");
        write(write_fd, buffer, strlen(buffer));
        for (int i = 0; i < 6; i++) {
            snprintf(buffer, sizeof(buffer), "%2d. %-14s | %d %s\n",
                     i + 1, categories[i], game_state->player_scores[player_id][i][0],
                     (game_state->player_scores[player_id][i][1] ? "(Scored)" : "(Unscored)"));
            write(write_fd, buffer, strlen(buffer));
        }

        snprintf(buffer, sizeof(buffer), "\nLower Section\n");
        write(write_fd, buffer, strlen(buffer));
        for (int i = 6; i < 13; i++) {
            snprintf(buffer, sizeof(buffer), "%2d. %-14s | %d %s\n",
                     i + 1, categories[i], game_state->player_scores[player_id][i][0],
                     (game_state->player_scores[player_id][i][1] ? "(Scored)" : "(Unscored)"));
            write(write_fd, buffer, strlen(buffer));
        }

        int upper_total = 0;
        for (int i = 0; i < 6; i++) upper_total += game_state->player_scores[player_id][i][0];

        if (game_state->bonus_achieved[player_id] == 'N') {
            int pts_to_bonus = (upper_total < 63) ? (63 - upper_total) : 0;
            snprintf(buffer, sizeof(buffer),
                     "\nYou need %d more points in the UPPER SECTION to receive the 35-point bonus.\n",
                     pts_to_bonus);
            write(write_fd, buffer, strlen(buffer));
        } else {
            snprintf(buffer, sizeof(buffer),
                     "\nUpper bonus achieved! (+35)\n");
            write(write_fd, buffer, strlen(buffer));
        }

        if (game_state->player_scores[player_id][14][1] == 1) {
            snprintf(buffer, sizeof(buffer),
                     "Yahtzee bonus total: %d\n", game_state->player_scores[player_id][14][0]);
            write(write_fd, buffer, strlen(buffer));
        }

        pthread_mutex_unlock(&game_state->game_mutex);

        snprintf(buffer, sizeof(buffer), "Turn complete. Waiting for other players...\n");
        write(write_fd, buffer, strlen(buffer));

        sem_post(&game_state->turn_done_sem[player_id]);

    next_turn:
        ;
    }

    // GAME OVER output (winner + table)
    pthread_mutex_lock(&game_state->game_mutex);
    int winner = game_state->winner_id;
    int my_final = game_state->final_scores[player_id];
    int finished = game_state->game_finished;
    pthread_mutex_unlock(&game_state->game_mutex);

    if (finished) {
        pthread_mutex_lock(&game_state->game_mutex);
        snprintf(buffer, sizeof(buffer),
                 "\n=== GAME OVER ===\nYour final score: %d\n",
                 my_final);
        write(write_fd, buffer, strlen(buffer));

        if (winner >= 0) {
            snprintf(buffer, sizeof(buffer),
                     "Winner: %s (Player %d) with %d\n",
                     game_state->player_names[winner], winner + 1,
                     game_state->final_scores[winner]);
            write(write_fd, buffer, strlen(buffer));
        } else {
            write(write_fd, "Winner: N/A\n", 12);
        }

        write(write_fd, "\nFinal Scores:\n", 14);
        for (int p = 0; p < MAX_PLAYERS; p++) {
            if (!game_state->participants[p]) continue;
            snprintf(buffer, sizeof(buffer), "Player %d (%s): %d\n",
                     p + 1, game_state->player_names[p], game_state->final_scores[p]);
            write(write_fd, buffer, strlen(buffer));
        }
        pthread_mutex_unlock(&game_state->game_mutex);
    }

    // Disconnect handling: if a participant disconnects early, auto-forfeit remaining (prevents deadlock)
    pthread_mutex_lock(&game_state->game_mutex);
    forfeit_remaining_on_disconnect_nolock(player_id);

    game_state->player_connected[player_id] = 0;
    game_state->active_players--;
    pthread_mutex_unlock(&game_state->game_mutex);

    snprintf(buffer, sizeof(buffer), "Disconnecting...\n");
    write(write_fd, buffer, strlen(buffer));

    close(write_fd);
    close(read_fd);

    printf("[SYSTEM] Player %d (%s) disconnected\n",
           player_id + 1, game_state->player_names[player_id]);
    exit(0);
}

// ------------------------------ Scheduler ------------------------------

void* scheduler_thread(void* arg) {
    (void)arg;
    printf("[SCHEDULER] RR Scheduler started (quantum=%ds)\n", QUANTUM_SECONDS);

    int turn_index = 0;

    while (1) {
        pthread_mutex_lock(&game_state->game_mutex);

        if (game_state->game_finished) {
            pthread_mutex_unlock(&game_state->game_mutex);
            break;
        }

        // Find next participant who isn't done
        int start = turn_index;
        while (!game_state->participants[turn_index] || game_state->player_done[turn_index]) {
            turn_index = (turn_index + 1) % MAX_PLAYERS;
            if (turn_index == start) break;
        }

        if (!game_state->participants[turn_index] || game_state->player_done[turn_index]) {
            // Nobody schedulable; either finished or broken state
            maybe_end_game_nolock();
            pthread_mutex_unlock(&game_state->game_mutex);
            nanosleep(&(struct timespec){0, 100000000}, NULL);
            continue;
        }

        game_state->current_turn = turn_index;
        game_state->turn_active[turn_index] = 1;
        pthread_mutex_unlock(&game_state->game_mutex);

        printf("[SCHEDULER] Turn -> Player %d (%ds quantum)\n",
               turn_index + 1, QUANTUM_SECONDS);

        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        pthread_mutex_lock(&game_state->game_mutex);
        game_state->turn_deadline[turn_index] = now;
        game_state->turn_deadline[turn_index].tv_sec += QUANTUM_SECONDS;
        pthread_mutex_unlock(&game_state->game_mutex);

        sem_post(&game_state->turn_sem[turn_index]);

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += QUANTUM_SECONDS;

        int r = sem_timedwait(&game_state->turn_done_sem[turn_index], &ts);

        pthread_mutex_lock(&game_state->game_mutex);
        game_state->turn_active[turn_index] = 0;
        pthread_mutex_unlock(&game_state->game_mutex);

        if (r == -1 && errno == ETIMEDOUT) {
            printf("[SCHEDULER] Player %d quantum expired\n", turn_index + 1);
        } else {
            printf("[SCHEDULER] Player %d completed their turn.\n", turn_index + 1);
        }

        // After each slice, re-check endgame
        pthread_mutex_lock(&game_state->game_mutex);
        maybe_end_game_nolock();
        pthread_mutex_unlock(&game_state->game_mutex);

        turn_index = (turn_index + 1) % MAX_PLAYERS;
    }

    printf("[SCHEDULER] Scheduler ending\n");
    return NULL;
}

// ------------------------------ Main ------------------------------

int main() {
    srand((unsigned)time(NULL));

    printf("\n");
    printf("╔════════════════════════════════════════════╗\n");
    printf("║    YAHTZEE SERVER (Single-Machine Mode)    ║\n");
    printf("║    CSN6214 Operating Systems Assignment    ║\n");
    printf("╚════════════════════════════════════════════╝\n");
    printf("\n");

    if (init_shared_memory() < 0) {
        fprintf(stderr, "Failed to initialize shared memory\n");
        return 1;
    }

    setup_signal_handlers();

    if (setup_ipc_server() < 0) {
        fprintf(stderr, "Failed to setup IPC\n");
        return 1;
    }

    printf("\nServer ready! Waiting for players...\n");
    printf("Host (Player 1) will choose how many players to start (3-%d)\n", MAX_PLAYERS);
    printf("----------------------------------------\n");

    pthread_t scheduler_tid;
    int scheduler_created = 0;

    int server_fd = open(SERVER_FIFO, O_RDWR | O_NONBLOCK);
    if (server_fd < 0) {
        perror("open server FIFO");
        return 1;
    }

    char accum[2048];
    size_t accum_len = 0;

    while (1) {
        char buf[256];
        int n = (int)read(server_fd, buf, sizeof(buf));
        if (n > 0) {
            size_t to_copy = (size_t)n;
            if (accum_len + to_copy >= sizeof(accum)) accum_len = 0;
            memcpy(accum + accum_len, buf, to_copy);
            accum_len += to_copy;

            size_t start = 0;
            for (size_t i = 0; i < accum_len; i++) {
                if (accum[i] == '\n') {
                    char client_fifo[256];
                    size_t line_len = i - start;
                    if (line_len >= sizeof(client_fifo)) line_len = sizeof(client_fifo) - 1;
                    memcpy(client_fifo, accum + start, line_len);
                    client_fifo[line_len] = '\0';
                    start = i + 1;

                    if (client_fifo[0] == '\0') continue;

                    pthread_mutex_lock(&game_state->game_mutex);
                    int already_started = game_state->game_started;
                    pthread_mutex_unlock(&game_state->game_mutex);

                    if (already_started) {
                        printf("[CONNECTION] Rejected - game already started\n");
                        reject_client(client_fifo,
                                      "Server: Game already started. Please restart server for a new game.\n");
                        continue;
                    }

                    printf("[CONNECTION] New connection request\n");

                    pthread_mutex_lock(&game_state->game_mutex);
                    int player_id = -1;
                    for (int p = 0; p < MAX_PLAYERS; p++) {
                        if (!game_state->player_connected[p]) {
                            player_id = p;
                            game_state->player_connected[p] = 1;
                            game_state->active_players++;
                            if (game_state->host_player_id < 0) game_state->host_player_id = p;
                            break;
                        }
                    }
                    int connected_now = game_state->active_players;
                    pthread_mutex_unlock(&game_state->game_mutex);

                    if (player_id == -1) {
                        printf("[CONNECTION] Rejected - server full\n");
                        reject_client(client_fifo,
                                      "Server: Full (max players reached). Try again later.\n");
                        continue;
                    }

                    printf("[CONNECTION] Player %d assigned (%d/%d connected)\n",
                           player_id + 1, connected_now, MAX_PLAYERS);

                    pid_t pid = fork();
                    if (pid < 0) {
                        perror("fork failed");
                        pthread_mutex_lock(&game_state->game_mutex);
                        game_state->player_connected[player_id] = 0;
                        game_state->active_players--;
                        pthread_mutex_unlock(&game_state->game_mutex);
                        reject_client(client_fifo, "Server: internal error (fork failed)\n");
                    } else if (pid == 0) {
                        handle_client(player_id, client_fifo);
                        exit(0);
                    } else {
                        printf("[FORK] Created child process PID %d for Player %d\n",
                               pid, player_id + 1);
                    }
                }
            }

            if (start > 0) {
                memmove(accum, accum + start, accum_len - start);
                accum_len -= start;
            }
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("read server FIFO");
        }

        // Start game when host has chosen target and enough players are connected
        pthread_mutex_lock(&game_state->game_mutex);
        int target = game_state->target_players;
        int connected = game_state->active_players;

        if (!scheduler_created && !game_state->game_started && target > 0 && connected >= target) {
            // NEW: freeze participants NOW
            game_state->participants_count = 0;
            for (int p = 0; p < MAX_PLAYERS; p++) {
                if (game_state->player_connected[p] && game_state->participants_count < target) {
                    game_state->participants[p] = 1;
                    game_state->participants_count++;
                } else {
                    game_state->participants[p] = 0;
                }
                game_state->player_done[p] = 0;
                game_state->final_scores[p] = 0;
            }
            game_state->winner_id = -1;

            game_state->game_started = 1;
            pthread_mutex_unlock(&game_state->game_mutex);

            pthread_create(&scheduler_tid, NULL, scheduler_thread, NULL);
            scheduler_created = 1;

            printf("\n*** GAME STARTING with %d players! ***\n\n", target);
        } else {
            pthread_mutex_unlock(&game_state->game_mutex);
        }

        struct timespec ts = {0, 100000000};
        nanosleep(&ts, NULL);
    }

    close(server_fd);
    munmap(game_state, sizeof(GameState));
    shm_unlink("/yahtzee_shm");
    return 0;
}
