#include <stdio.h>
#include <string.h> // for string functions
#include <stdbool.h> // for boolean functions

#include <pthread.h> // for multithreading (i haven't implemented this yet - alesha)
#include <unistd.h> // for sleep()

#include <stdlib.h> // for randomized functions
#include <time.h> // for the time() function

// play state (running or ended)
bool play_state = true;

// initializing the 'dice' array for all the players
int p_dice[6] = { 0, 0, 0, 0, 0, 0 };

int reroll_1 = 0, reroll_2 = 0, reroll_3 = 0, reroll_4 = 0, reroll_5 = 0;

// random number
int rand_num;

// yes or no
char yes_no;

// amount of dice to reroll
int reroll_amount;

int main() {
    srand(time(NULL));

    printf("Rolling dice...\n");

    for (int i = 0; i <= 5; i++) {
        rand_num = rand() % 6 + 1;
        p_dice[i] = rand_num;
    }

    for (int i = 0; i <= 5; i++) {
        printf("[%d] ",p_dice[i]);
    }

    printf("\n\nWould you like to re-roll any of the dices? (Y/N)\n");
    scanf("%c", &yes_no);

    if (yes_no == 'Y'){
        printf("\nHow many dice would you like to reroll? (0-6)\n");
        scanf("%d", &reroll_amount);
        if(reroll_amount > 0 && reroll_amount < 6){
            printf("\nPlease input which dice you would like to reroll, from left to right, with spaces.\n");
            switch (reroll_amount) {
                case 1:
                    scanf("%d", &reroll_1);
                    printf("hi!");
                    break;
                case 2:
                    scanf("%d %d", &reroll_1, &reroll_2);
                    break;
                case 3:
                    scanf("%d %d %d", &reroll_1, &reroll_2, &reroll_3);
                    break;
                case 4:
                    scanf("%d %d %d %d", &reroll_1, &reroll_2, &reroll_3, &reroll_4);
                    break;
                case 5:
                    scanf("%d %d %d %d %d", &reroll_1, &reroll_2, &reroll_3, &reroll_4, &reroll_5);
                    break;
                default:
                    break;
            }
        }
    }

    return 0;
}