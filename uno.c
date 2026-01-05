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
int p_dice[5] = { 0, 0, 0, 0, 0 };

// i took this from geeks4geeks for qsort (ref: https://www.geeksforgeeks.org/c/qsort-function-in-c/)
int n = sizeof(p_dice) / sizeof(p_dice[0]);

int comp(const void *a, const void *b) {
    return (*(int *)a - *(int *)b);
}

// which dice needs to be rerolled
int p_reroll[6] = { 0, 0, 0, 0, 0 };

// player's score
int p_score = 0;

// score, if it's checked or not
int scoring[15][3] = {
  // upper section
  {0, 0, 0}, // aces, #0 (count and add only aces)
  {0, 0, 0}, // twos, #1 (count and add only twos)
  {0, 0, 0}, // threes, #2 (count and add only threes)
  {0, 0, 0}, // fours, #3 (count and add only fours)
  {0, 0, 0}, // fives, #4 (count and add only fives)
  {0, 0, 0}, // sixes, #5 (count and add only sixes)
  {0, 0, 0}, // bonus, #6 (if total of all upper section boxes is >= 63, add a 35-point bonus)
  // lower section
  {0, 0, 0}, // three-of-a-kind, #7 (sum of all 5 dice if at least 3 dice match)
  {0, 0, 0}, // four-of-a-kind, #8 (sum of all 5 dice if at least 4 dice match)
  {0, 0, 0}, // full house, #9 (25 points for any set of 3 matching dice + a pair of matching dice)
  {0, 0, 0}, // small straight, #10 (30 points for any four sequential dice)
  {0, 0, 0}, // large straight, #11 (40 points for any five sequential dice)
  {0, 0, 0}, // yahtzee, #12 (50 points for all five dice being the same)
  {0, 0, 0}, // chance, #13 (sum of all 5 dice)
  {0, 0, 0} // yahtzee bonus, #14
};

// amount of times a player can reroll
int add_reroll = 2;

// random number
int rand_num;

// yes or no
char yes_no;

// amount of dice to reroll
int reroll_amount;

// for small straight and full straight, counting the number that are in sequential order
int counter;

int main() {
    srand(time(NULL));

    printf("Rolling dice...\n");

    for (int i = 0; i <= 4; i++) {
        rand_num = rand() % 6 + 1;
        p_dice[i] = rand_num;
    }

    for (int i = 0; i <= 4; i++) {
        printf("[%d] ",p_dice[i]);
    }

    while (add_reroll > 0){
        printf("\n\nWould you like to re-roll any of the dices? (Y/N)\n");
        scanf(" %c", &yes_no);

        if (yes_no == 'Y'){
            add_reroll -= 1;
            printf("\nHow many dice would you like to reroll? (0-6)\n");
            scanf("%d", &reroll_amount);
            if(reroll_amount > 0 && reroll_amount <= 5){
                if(reroll_amount > 0 && reroll_amount <= 4){
                    printf("\nPlease input which dice you would like to reroll, from left to right, with spaces.\n");
                }
                switch (reroll_amount) {
                    case 1:
                        scanf("%d", &p_reroll[0]);
                        break;
                    case 2:
                        scanf("%d %d", &p_reroll[0], &p_reroll[1]);
                        break;
                    case 3:
                        scanf("%d %d %d", &p_reroll[0], &p_reroll[1], &p_reroll[2]);
                        break;
                    case 4:
                        scanf("%d %d %d %d", &p_reroll[0], &p_reroll[1], &p_reroll[2], &p_reroll[3]);
                        break;
                    case 5:
                        for (int i = 0; i <= 4; i++) {
                            p_reroll[i] = i + 1;
                        }
                        break;
                    default:
                        break;
                }

                for (int i = 0; i <= 4; i++) {
                    if(p_reroll[i] != 0){
                        rand_num = rand() % 6 + 1;
                        p_dice[p_reroll[i] - 1] = rand_num;
                        p_reroll[i] = 0;
                    }
                }

                printf("\nRerolling dice...\n");
                for (int i = 0; i <= 4; i++) {
                    printf("[%d] ",p_dice[i]);
                }
            }
        }
        else if (yes_no == 'N'){
            break;
        }
    }

    // sort the dice array, ascending
    qsort(p_dice, n, sizeof(p_dice[0]), comp);

    // calculating dice scores
    // upper section
    for (int i = 0; i <= 4; i++) {
        // aces
        if(p_dice[i] == 1){
            scoring[0][2] += 1;
        }
        // twos
        if(p_dice[i] == 2){
            scoring[1][2] += 2;
        }
        // threes
        if(p_dice[i] == 3){
            scoring[2][2] += 3;
        }
        // fours
        if(p_dice[i] == 4){
            scoring[3][2] += 4;
        }
        // fives
        if(p_dice[i] == 5){
            scoring[4][2] += 5;
        }
        // sixes
        if(p_dice[i] == 6){
            scoring[5][2] += 6;
        }
    }

    // lower section
    // three-of-a-kind
    if((p_dice[0] == p_dice[1] && p_dice[1] == p_dice[2]) || (p_dice[1] == p_dice[2] && p_dice[2] == p_dice[3]) || (p_dice[2] == p_dice[3] && p_dice[3] == p_dice[4])){
        for (int i = 0; i <= 4; i++) {
            scoring[7][2] += p_dice[i];
        }
    }

    // four-of-a-kind
    if((p_dice[0] == p_dice[1] && p_dice[1] == p_dice[2] && p_dice[2] == p_dice[3]) || (p_dice[1] == p_dice[2] && p_dice[2] == p_dice[3] && p_dice[3] == p_dice[4])){
        for (int i = 0; i <= 4; i++) {
            scoring[8][2] += p_dice[i];
        }
    }

    // full house
    if(((p_dice[0] == p_dice[1] && p_dice[1] == p_dice[2]) && p_dice[3] == p_dice[4]) || (p_dice[0] == p_dice[1] && (p_dice[2] == p_dice[3] && p_dice[3] == p_dice[4]))){
        for (int i = 0; i <= 4; i++) {
            scoring[9][2] += p_dice[i];
        }
    }

    // small straight (1-2-3-4, 2-3-4-5, 3-4-5-6)
    counter = 0;
    for (int i = 0; i <= 4; i++) {
        if(p_dice[i] == p_dice[i + 1] - 1){
            counter += 1;
        }
    }
    if(counter >= 4){
        scoring[10][2] += 30;
    }

    // full straight (1-2-3-4-5, 2-3-4-5-6)
    counter = 0;
    for (int i = 0; i <= 4; i++) {
        if(p_dice[i] == p_dice[i + 1] - 1){
            counter += 1;
        }
    }
    if(counter == 5){
        scoring[11][2] += 30;
    }

    // yahtzee
    counter = 0;
    for (int i = 0; i <= 4; i++) {
        if(p_dice[i] == p_dice[i + 1]){
            counter += 1;
        }
    }
    if(counter == 5){
        scoring[12][2] += 50;
    }

    // chance
    for (int i = 0; i <= 4; i++) {
        scoring[13][2] += p_dice[i];
    }

    printf("\nPlease select where you'd like to score...");
    printf("\nUpper Section");
    if(scoring[0][1] != 1)
        printf("\n1. Aces            | Possible score: %d", scoring[0][2]);
    if(scoring[1][1] != 1)
        printf("\n2. Twos            | Possible score: %d", scoring[1][2]);
    if(scoring[2][1] != 1)
        printf("\n3. Threes          | Possible score: %d", scoring[2][2]);
    if(scoring[3][1] != 1)
        printf("\n4. Fours           | Possible score: %d", scoring[3][2]);
    if(scoring[4][1] != 1)
        printf("\n5. Fives           | Possible score: %d", scoring[4][2]);
    if(scoring[5][1] != 1)
        printf("\n6. Sixes           | Possible score: %d", scoring[5][2]);
    printf("\nLower Section");
    if(scoring[7][1] != 1)
        printf("\n7. Three-of-a-Kind | Possible score: %d", scoring[7][2]);
    if(scoring[8][1] != 1)
        printf("\n8. Four-of-a-Kind  | Possible score: %d", scoring[8][2]);
    if(scoring[9][1] != 1)
        printf("\n9. Full House      | Possible score: %d", scoring[9][2]);
    if(scoring[10][1] != 1)
        printf("\n10. Small Straight | Possible score: %d", scoring[10][2]);
    if(scoring[11][1] != 1)
        printf("\n11. Large Straight | Possible score: %d", scoring[11][2]);
    if(scoring[12][1] != 1)
        printf("\n12. Yahtzee        | Possible score: %d", scoring[12][2]);
    if(scoring[13][1] != 1)
        printf("\n13. Chance         | Possible score: %d", scoring[13][2]);

    return 0;
}