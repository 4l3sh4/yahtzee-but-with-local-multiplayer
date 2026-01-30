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

// Configuration
#define MAX_PLAYERS 5
#define MAX_ROUNDS 13
#define NAME_SIZE 50
#define BUFFER_SIZE 2048
#define FIFO_DIR "/tmp/yahtzee"
#define SERVER_FIFO "/tmp/yahtzee/server_fifo"

// Shared Memory Structure
typedef struct {
    int current_turn;
    int active_players;
    int game_started;
    int game_round;
    int game_finished;
    char player_names[MAX_PLAYERS][NAME_SIZE];
    int player_connected[MAX_PLAYERS];
    int player_dice[MAX_PLAYERS][5];
    int player_rerolls_left[MAX_PLAYERS];
    int player_scores[MAX_PLAYERS][15][3];
    pthread_mutex_t game_mutex;
    pthread_mutex_t log_mutex;
    sem_t turn_sem[MAX_PLAYERS];
    int total_wins[MAX_PLAYERS];
} GameState;

GameState *game_state;

// HELPER FUNCTIONS [ALESHA]

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


// GAME LOGIC [ALESHA]

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
    
    for (int i = 0; i < 13; i++) game_state->player_scores[player_id][i][2] = 0;
    
    // Upper section
    for (int i = 0; i < 5; i++) {
        if (dice[i] == 1) game_state->player_scores[player_id][0][2] += 1;
        if (dice[i] == 2) game_state->player_scores[player_id][1][2] += 2;
        if (dice[i] == 3) game_state->player_scores[player_id][2][2] += 3;
        if (dice[i] == 4) game_state->player_scores[player_id][3][2] += 4;
        if (dice[i] == 5) game_state->player_scores[player_id][4][2] += 5;
        if (dice[i] == 6) game_state->player_scores[player_id][5][2] += 6;
    }
    
    // Three of a kind
    if (has_n_of_a_kind(dice, 3)) {
        int sum = 0;
        for (int i = 0; i < 5; i++) sum += dice[i];
        game_state->player_scores[player_id][6][2] = sum;
    }
    
    // Four of a kind
    if (has_n_of_a_kind(dice, 4)) {
        int sum = 0;
        for (int i = 0; i < 5; i++) sum += dice[i];
        game_state->player_scores[player_id][7][2] = sum;
    }
    
    if (is_full_house(dice)) game_state->player_scores[player_id][8][2] = 25;
    if (has_small_straight(dice)) game_state->player_scores[player_id][9][2] = 30;
    if (has_large_straight(dice)) game_state->player_scores[player_id][10][2] = 40;
    if (has_n_of_a_kind(dice, 5)) game_state->player_scores[player_id][11][2] = 50;
    
    // Chance
    int sum = 0;
    for (int i = 0; i < 5; i++) sum += dice[i];
    game_state->player_scores[player_id][12][2] = sum;
    
    pthread_mutex_unlock(&game_state->game_mutex);
}

int apply_score(int player_id, int category) {
    pthread_mutex_lock(&game_state->game_mutex);
    if (category < 0 || category >= 13 || game_state->player_scores[player_id][category][1] == 1) {
        pthread_mutex_unlock(&game_state->game_mutex);
        return 0;
    }
    game_state->player_scores[player_id][category][0] = game_state->player_scores[player_id][category][2];
    game_state->player_scores[player_id][category][1] = 1;
    pthread_mutex_unlock(&game_state->game_mutex);
    return 1;
}

int calculate_total_score(int player_id) {
    int total = 0, upper_total = 0;
    pthread_mutex_lock(&game_state->game_mutex);
    for (int i = 0; i < 6; i++) upper_total += game_state->player_scores[player_id][i][0];
    if (upper_total >= 63) {
        game_state->player_scores[player_id][13][0] = 35;
        total += 35;
    }
    for (int i = 0; i < 13; i++) total += game_state->player_scores[player_id][i][0];
    pthread_mutex_unlock(&game_state->game_mutex);
    return total;
}


// SHARED MEMORY INITIALIZATION [ARIANA]

int init_shared_memory() {
    int shm_fd = shm_open("/yahtzee_shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return -1;
    }
    
    if (ftruncate(shm_fd, sizeof(GameState)) == -1) {
        perror("ftruncate failed");
        return -1;
    }
    
    game_state = (GameState*) mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (game_state == MAP_FAILED) {
        perror("mmap failed");
        return -1;
    }
    
    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&game_state->game_mutex, &mutex_attr);
    pthread_mutex_init(&game_state->log_mutex, &mutex_attr);
    pthread_mutexattr_destroy(&mutex_attr);
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        sem_init(&game_state->turn_sem[i], 1, 0);
    }
    
    game_state->current_turn = 0;
    game_state->active_players = 0;
    game_state->game_started = 0;
    game_state->game_round = 1;
    game_state->game_finished = 0;
    
    for (int i = 0; i < MAX_PLAYERS; i++) {
        game_state->player_connected[i] = 0;
        memset(game_state->player_names[i], 0, NAME_SIZE);
        for (int j = 0; j < 15; j++) {
            for (int k = 0; k < 3; k++) {
                game_state->player_scores[i][j][k] = 0;
            }
        }
    }
    
    printf("✓ Shared memory initialized\n");
    return 0;
}


// ZOMBIE REAPING (SIGCHLD) [ARIANA]

void sigchld_handler(int sig) {
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


// IPC SETUP (Named Pipes) [ARIANA]

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


// CLIENT HANDLER (Child Process) [ARIANA]

void handle_client(int player_id, const char* client_fifo) {
    char buffer[BUFFER_SIZE];
    char recv_buffer[256];
    char client_read_fifo[256];
    
    snprintf(client_read_fifo, sizeof(client_read_fifo), "%s_read", client_fifo);
    
    int write_fd = open(client_fifo, O_WRONLY);
    int read_fd = open(client_read_fifo, O_RDONLY);
    
    if (write_fd < 0 || read_fd < 0) {
        perror("open FIFOs failed");
        exit(1);
    }
    
    // Get player name
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
    
    snprintf(buffer, sizeof(buffer), "Waiting for game to start...\n");
    write(write_fd, buffer, strlen(buffer));
    
    while (!game_state->game_started) {
        sleep(1);
    }
    
    snprintf(buffer, sizeof(buffer), "\n*** GAME STARTING! ***\n\n");
    write(write_fd, buffer, strlen(buffer));
    
    // Game loop
    const char *categories[] = {
        "Aces", "Twos", "Threes", "Fours", "Fives", "Sixes",
        "Three of a Kind", "Four of a Kind", "Full House",
        "Small Straight", "Large Straight", "Yahtzee", "Chance"
    };
    
    for (int round = 1; round <= MAX_ROUNDS && !game_state->game_finished; round++) {
        sem_wait(&game_state->turn_sem[player_id]);
        
        if (!game_state->player_connected[player_id]) break;
        
        snprintf(buffer, sizeof(buffer),
                 "\n========================================\n"
                 "[ROUND %d - YOUR TURN, %s]\n"
                 "========================================\n",
                 round, game_state->player_names[player_id]);
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
        
        // Reroll phase
        while (game_state->player_rerolls_left[player_id] > 0) {
            snprintf(buffer, sizeof(buffer), "\nRerolls left: %d. Reroll? (Y/N): ",
                     game_state->player_rerolls_left[player_id]);
            write(write_fd, buffer, strlen(buffer));
            
            n = read(read_fd, recv_buffer, sizeof(recv_buffer));
            if (n <= 0) break;
            
            if (recv_buffer[0] == 'N' || recv_buffer[0] == 'n') break;
            
            if (recv_buffer[0] == 'Y' || recv_buffer[0] == 'y') {
                snprintf(buffer, sizeof(buffer), "Which dice? (e.g., 1 3 5): ");
                write(write_fd, buffer, strlen(buffer));
                
                n = read(read_fd, recv_buffer, sizeof(recv_buffer));
                if (n <= 0) break;
                
                int dice_to_reroll[5];
                int count = 0;
                char *token = strtok(recv_buffer, " \n");
                while (token && count < 5) {
                    int die = atoi(token);
                    if (die >= 1 && die <= 5) {
                        dice_to_reroll[count++] = die;
                    }
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
        
        snprintf(buffer, sizeof(buffer), "\n=== SCORING OPTIONS ===\n");
        write(write_fd, buffer, strlen(buffer));
        
        for (int i = 0; i < 13; i++) {
            if (game_state->player_scores[player_id][i][1] == 0) {
                snprintf(buffer, sizeof(buffer), "%2d. %-20s | %d points\n",
                         i + 1, categories[i],
                         game_state->player_scores[player_id][i][2]);
                write(write_fd, buffer, strlen(buffer));
            }
        }
        
        int valid = 0, choice;
        while (!valid) {
            snprintf(buffer, sizeof(buffer), "\nChoose category (1-13): ");
            write(write_fd, buffer, strlen(buffer));
            
            n = read(read_fd, recv_buffer, sizeof(recv_buffer));
            if (n <= 0) break;
            
            choice = atoi(recv_buffer);
            if (choice >= 1 && choice <= 13 && 
                game_state->player_scores[player_id][choice - 1][1] == 0) {
                valid = 1;
            } else {
                snprintf(buffer, sizeof(buffer), "Invalid choice! Try again.\n");
                write(write_fd, buffer, strlen(buffer));
            }
        }
        
        if (apply_score(player_id, choice - 1)) {
            snprintf(buffer, sizeof(buffer), "Scored %d points in %s!\n",
                     game_state->player_scores[player_id][choice - 1][0],
                     categories[choice - 1]);
            write(write_fd, buffer, strlen(buffer));
        }
        
        snprintf(buffer, sizeof(buffer), "Turn complete. Waiting for other players...\n");
        write(write_fd, buffer, strlen(buffer));
    }
    
    if (game_state->game_finished) {
        int final_score = calculate_total_score(player_id);
        snprintf(buffer, sizeof(buffer),
                 "\n=== GAME OVER ===\nYour final score: %d\n", final_score);
        write(write_fd, buffer, strlen(buffer));
    }
    
    pthread_mutex_lock(&game_state->game_mutex);
    game_state->player_connected[player_id] = 0;
    game_state->active_players--;
    pthread_mutex_unlock(&game_state->game_mutex);
    
    snprintf(buffer, sizeof(buffer), "Disconnecting...\n");
    write(write_fd, buffer, strlen(buffer));
    
    close(write_fd);
    close(read_fd);
    
    printf("[SYSTEM] Player %d (%s) disconnected\n",
           player_id + 1, game_state->player_names[player_id]);
}


// ROUND ROBIN SCHEDULER [AMIRAH]
// The current scheduler is the simplified one to test synchronization
// Need more improvement


void* scheduler_thread(void* arg) {
    printf("[SCHEDULER] Scheduler thread started\n");
    
    int turn_index = 0;
    
    while (!game_state->game_finished) {
        pthread_mutex_lock(&game_state->game_mutex);
        
        if (game_state->player_connected[turn_index]) {
            game_state->current_turn = turn_index;
            pthread_mutex_unlock(&game_state->game_mutex);
            
            printf("[SCHEDULER] Signaling Player %d's turn\n", turn_index + 1);
            sem_post(&game_state->turn_sem[turn_index]);
            
            sleep(2);
        } else {
            pthread_mutex_unlock(&game_state->game_mutex);
        }
        
        turn_index = (turn_index + 1) % MAX_PLAYERS;
        if (turn_index >= game_state->active_players) turn_index = 0;
        
        sleep(1);
    }
    
    printf("[SCHEDULER] Scheduler thread ending\n");
    return NULL;
}


// MAIN FUNCTION [ARIANA]

int main() {
    srand(time(NULL));
    
    printf("\n");
    printf("╔═══════════════════════════════════════════╗\n");
    printf("║  YAHTZEE SERVER (Single-Machine Mode)    ║\n");
    printf("║  CSN6214 Operating Systems Assignment    ║\n");
    printf("╚═══════════════════════════════════════════╝\n");
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
    printf("(Need 3-5 players to start)\n");
    printf("----------------------------------------\n");
    
    pthread_t scheduler_tid;
    int scheduler_created = 0;
    
    while (1) {
        int server_fd = open(SERVER_FIFO, O_RDONLY);
        if (server_fd < 0) {
            perror("open server FIFO");
            continue;
        }
        
        char client_fifo[256];
        int n = read(server_fd, client_fifo, sizeof(client_fifo) - 1);
        close(server_fd);
        
        if (n <= 0) continue;
        
        client_fifo[n] = '\0';
        client_fifo[strcspn(client_fifo, "\n")] = '\0';
        
        printf("[CONNECTION] New connection request\n");
        
        pthread_mutex_lock(&game_state->game_mutex);
        int player_id = -1;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (!game_state->player_connected[i]) {
                player_id = i;
                game_state->player_connected[i] = 1;
                game_state->active_players++;
                break;
            }
        }
        pthread_mutex_unlock(&game_state->game_mutex);
        
        if (player_id == -1) {
            printf("[CONNECTION] Rejected - server full\n");
            continue;
        }
        
        printf("[CONNECTION] Player %d assigned (%d/%d players)\n",
               player_id + 1, game_state->active_players, MAX_PLAYERS);
        
        pid_t pid = fork();
        
        if (pid < 0) {
            perror("fork failed");
            pthread_mutex_lock(&game_state->game_mutex);
            game_state->player_connected[player_id] = 0;
            game_state->active_players--;
            pthread_mutex_unlock(&game_state->game_mutex);
        } else if (pid == 0) {
            handle_client(player_id, client_fifo);
            exit(0);
        } else {
            printf("[FORK] Created child process PID %d for Player %d\n", pid, player_id + 1);
            
            if (game_state->active_players >= 3 && !scheduler_created) {
                game_state->game_started = 1;
                pthread_create(&scheduler_tid, NULL, scheduler_thread, NULL);
                scheduler_created = 1;
                printf("\n*** GAME STARTING with %d players! ***\n\n", game_state->active_players);
            }
        }
    }
    
    munmap(game_state, sizeof(GameState));
    shm_unlink("/yahtzee_shm");
    return 0;
}
