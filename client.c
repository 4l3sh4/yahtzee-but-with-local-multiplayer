#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define BUFFER_SIZE 2048
#define FIFO_DIR "/tmp/yahtzee"
#define SERVER_FIFO "/tmp/yahtzee/server_fifo"

int main() {
    char client_write_fifo[256];
    char client_read_fifo[256];
    char buffer[BUFFER_SIZE];
    char input[256];
    int server_fd, write_fd, read_fd;
    
    printf("\n");
    printf("╔═══════════════════════════════════════╗\n");
    printf("║              YAHTZEE GAME             ║\n");
    printf("║   SINGLE-MACHINE MODE (Named Pipes)   ║\n");
    printf("╚═══════════════════════════════════════╝\n");
    printf("\n");
    
    // Create unique FIFO names for this client
    snprintf(client_write_fifo, sizeof(client_write_fifo), 
             "%s/client_%d", FIFO_DIR, getpid());
    snprintf(client_read_fifo, sizeof(client_read_fifo), 
             "%s/client_%d_read", FIFO_DIR, getpid());
    
    // Create client FIFOs
    unlink(client_write_fifo);
    unlink(client_read_fifo);
    
    if (mkfifo(client_write_fifo, 0666) == -1) {
        perror("mkfifo client_write failed");
        return 1;
    }
    
    if (mkfifo(client_read_fifo, 0666) == -1) {
        perror("mkfifo client_read failed");
        unlink(client_write_fifo);
        return 1;
    }
    
    printf("Connecting to server...\n");
    
    // Open server FIFO and send our FIFO name
    server_fd = open(SERVER_FIFO, O_WRONLY);
    if (server_fd < 0) {
        perror("Cannot connect to server");
        printf("\nMake sure the server is running!\n");
        unlink(client_write_fifo);
        unlink(client_read_fifo);
        return 1;
    }
    
    // Send our FIFO name to server
    {
        char line[512];
        // send newline so server can parse one FIFO path per line
        snprintf(line, sizeof(line), "%s\n", client_write_fifo);
        if (write(server_fd, line, strlen(line)) < 0) {
            perror("write to server fifo failed");
        }
    }
    close(server_fd);

    
    // Open our FIFOs for communication
    read_fd = open(client_write_fifo, O_RDONLY);
    if (read_fd < 0) {
        perror("open read_fd failed");
        unlink(client_write_fifo);
        unlink(client_read_fifo);
        return 1;
    }
    
    write_fd = open(client_read_fifo, O_WRONLY);
    if (write_fd < 0) {
        perror("open write_fd failed");
        close(read_fd);
        unlink(client_write_fifo);
        unlink(client_read_fifo);
        return 1;
    }
    
    printf("✓ Connected to server!\n");
    printf("===============================================\n\n");
    
    // Main communication loop
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int n = read(read_fd, buffer, BUFFER_SIZE - 1);
        
        if (n <= 0) {
            if (n < 0) {
                perror("\nConnection error");
            } else {
                printf("\nServer disconnected\n");
            }
            break;
        }
        
        // Display what server sent
        printf("%s", buffer);
        fflush(stdout);
        
        // Check if server is asking for input.
        int needs_input = 0;

        // Common gameplay prompts
        if (strstr(buffer, "Enter your name")) needs_input = 1;
        if (strstr(buffer, "Reroll?")) needs_input = 1;
        if (strstr(buffer, "Which dice")) needs_input = 1;
        if (strstr(buffer, "Choose category")) needs_input = 1;
        if (strstr(buffer, "Choose LOWER category")) needs_input = 1;
        if (strstr(buffer, "Choose where")) needs_input = 1;

        // Host lobby prompt
        if (strstr(buffer, "Enter number of players")) needs_input = 1;

        {
            size_t len = strlen(buffer);
            if (len >= 2 && buffer[len - 2] == ':' && buffer[len - 1] == ' ') {
                needs_input = 1;
            }
        }

        if (needs_input) {
            
            // Get user input
            if (fgets(input, sizeof(input), stdin) != NULL) {
                // Send to server
                if (write(write_fd, input, strlen(input)) < 0) {
                    perror("Send failed");
                    break;
                }
            }
        }
    }
    
    printf("\n");
    printf("===============================================\n");
    printf("Thank you for playing Yahtzee!\n");
    printf("===============================================\n\n");
    
    // Cleanup
    close(read_fd);
    close(write_fd);
    unlink(client_write_fifo);
    unlink(client_read_fifo);
    
    return 0;
}
