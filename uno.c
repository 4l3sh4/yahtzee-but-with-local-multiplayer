#include <stdio.h>
#include <string.h> // for string functions
#include <stdbool.h> // for boolean functions

#include <pthread.h> // for multithreading (i haven't implemented this yet - alesha)
#include <unistd.h> // for sleep()

#include <stdlib.h> // for randomized functions
#include <time.h> // for the time() function

// current amount of turns
// game ends at turn 13
int turn_count = 1;

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

// score, if it's checked or not, possible score
int scoring[15][3] = {
  // upper section
  {0, 0, 0}, // aces, #0 (count and add only aces)
  {0, 0, 0}, // twos, #1 (count and add only twos)
  {0, 0, 0}, // threes, #2 (count and add only threes)
  {0, 0, 0}, // fours, #3 (count and add only fours)
  {0, 0, 0}, // fives, #4 (count and add only fives)
  {0, 0, 0}, // sixes, #5 (count and add only sixes)
  {0, 0, 0}, // three-of-a-kind, #6 (sum of all 5 dice if at least 3 dice match)
  {0, 0, 0}, // four-of-a-kind, #7 (sum of all 5 dice if at least 4 dice match)
  {0, 0, 0}, // full house, #8 (25 points for any set of 3 matching dice + a pair of matching dice)
  {0, 0, 0}, // small straight, #9 (30 points for any four sequential dice)
  {0, 0, 0}, // large straight, #10 (40 points for any five sequential dice)
  {0, 0, 0}, // yahtzee, #11 (50 points for all five dice being the same)
  {0, 0, 0}, // chance, #12 (sum of all 5 dice)
  {0, 0, 0}, // bonus, #13 (if total of all upper section boxes is >= 63, add a 35-point bonus)
  // lower section
  {0, 0, 0} // yahtzee bonus, #14 (100 bonus points)
};

// amount of times a player can reroll
int add_reroll = 2;

// random number
int rand_num;

// yes or no
char yes_no;

// amount of dice to reroll
int reroll_amount;

// where the player wants to score
int score_where;

// for small straight and full straight, counting the number that are in sequential order
int counter;

// when an additional yahtzee is scored and an upper limit box is available
// and matches the yahtzee's dices, they MUST score it there
int required_upper_section;

// if player has scored a yahtzee for the first time and filled in the box
char yahtzee_achieved = 'N';

// amount of yahtzees
int amount_yahtzee;

// when the player can only score the lower sections after the upper section has been fileld (Joker rules)
char lower_section_only = 'N';

// skip the scoring phase (applies when the player is forced to score the upper section after scoring an additional yahtzee)
char skip_scoring = 'N';

// if player has already scored a bonus
char bonus_achieved = 'N';

// calculate upper section for bonus purposes
int calc_upper;

// amount of points left in the upper section to achieve a 35-point bonus
int points_to_bonus;

// is any of the two sections are filled or not
char upper_section_filled = 'N';
char lower_section_filled = 'N';

// scoring stuff
int upper_total_score;
int lower_total_score;

int main() {
    srand(time(NULL));

    while(turn_count < 14){
        printf("\n\n[TURN %d]", turn_count);

        printf("\n\nRolling dice...\n");

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
            while (yes_no != 'Y' && yes_no != 'N'){
                printf("\nInvalid input!");
                printf("\nWould you like to re-roll any of the dices? (Y/N)\n");
                scanf(" %c", &yes_no);
            }

            if (yes_no == 'Y'){
                add_reroll -= 1;
                printf("\nHow many dice would you like to reroll? (0-5)\n");
                scanf("%d", &reroll_amount);
                while(reroll_amount <= 0 || reroll_amount > 5){
                    printf("\nInvalid input!");
                    printf("\nHow many dice would you like to reroll? (0-5)\n");
                    scanf("%d", &reroll_amount);
                }
                if(reroll_amount > 0 && reroll_amount <= 5){
                    if(reroll_amount > 0 && reroll_amount <= 4){
                        printf("\nPlease input which dice you would like to reroll, from left to right, with spaces.\n");
                    }
                    switch (reroll_amount) {
                        case 0:
                            break;
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

        for (int i = 0; i <= 4; i++) {
            p_dice[i] = 6;
        }

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
                scoring[6][2] += p_dice[i];
            }
        }

        // four-of-a-kind
        if((p_dice[0] == p_dice[1] && p_dice[1] == p_dice[2] && p_dice[2] == p_dice[3]) || (p_dice[1] == p_dice[2] && p_dice[2] == p_dice[3] && p_dice[3] == p_dice[4])){
            for (int i = 0; i <= 4; i++) {
                scoring[7][2] += p_dice[i];
            }
        }

        // full house
        if(((p_dice[0] == p_dice[1] && p_dice[1] == p_dice[2]) && p_dice[3] == p_dice[4]) || (p_dice[0] == p_dice[1] && (p_dice[2] == p_dice[3] && p_dice[3] == p_dice[4]))){
            scoring[8][2] = 25;
        }

        // small straight (1-2-3-4, 2-3-4-5, 3-4-5-6)
        counter = 0;
        for (int i = 0; i <= 3; i++) {
            if(p_dice[i] == p_dice[i + 1] - 1){
                counter += 1;
            }
        }
        if(counter >= 3){
            scoring[9][2] = 30;
        }

        // large straight (1-2-3-4-5, 2-3-4-5-6)
        counter = 0;
        for (int i = 0; i <= 3; i++) {
            if(p_dice[i] == p_dice[i + 1] - 1){
                counter += 1;
            }
        }
        if(counter == 4){
            scoring[10][2] = 40;
        }

        // yahtzee
        counter = 0;
        for (int i = 0; i <= 3; i++) {
            if(p_dice[i] == p_dice[i + 1]){
                counter += 1;
            }
        }
        if(counter == 4){
            scoring[11][2] = 50;
            required_upper_section = p_dice[0] - 1;
        }

        // chance
        for (int i = 0; i <= 4; i++) {
            scoring[12][2] += p_dice[i];
        }

        // joker rules
        if (yahtzee_achieved == 'Y' && scoring[11][2] == 50){
            scoring[8][2] = 25;
            scoring[9][2] = 30;
            scoring[10][2] = 40;
        }

        if(upper_section_filled == 'N'){
            printf("\n\nUpper Section");
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
        }
        if(lower_section_filled == 'N'){
            printf("\nLower Section");
            if(scoring[6][1] != 1)
                printf("\n7. Three-of-a-Kind | Possible score: %d", scoring[6][2]);
            if(scoring[7][1] != 1)
                printf("\n8. Four-of-a-Kind  | Possible score: %d", scoring[7][2]);
            if(scoring[8][1] != 1)
                printf("\n9. Full House      | Possible score: %d", scoring[8][2]);
            if(scoring[9][1] != 1)
                printf("\n10. Small Straight | Possible score: %d", scoring[9][2]);
            if(scoring[10][1] != 1)
                printf("\n11. Large Straight | Possible score: %d", scoring[10][2]);
            if(scoring[11][1] != 1)
                printf("\n12. Yahtzee        | Possible score: %d", scoring[11][2]);
            if(scoring[12][1] != 1)
                printf("\n13. Chance         | Possible score: %d", scoring[12][2]);
        }
        
        // if player scores an additional yahtzee
        if((scoring[11][2] == 50) && (amount_yahtzee >= 1)){
            printf("\n\nCongratulations! You scored another Yahtzee!");
            amount_yahtzee += 1;
            // if the player has scored a yahtzee in the yahtzee box, give them 100 bonus points
            if(yahtzee_achieved == 'Y'){
                scoring[14][0] += 100;
                scoring[14][1] = 1;
            }
            // checks if the yahtzee box has been filled to apply Joker rules
            if(scoring[11][1] == 1){
                // player must score in the upper section (if it has not been filled yet)
                if(scoring[required_upper_section][1] == 0){
                    scoring[required_upper_section][0] = scoring[required_upper_section][2];
                    scoring[required_upper_section][1] = 1;
                    printf("\nSince you have scored another Yahtzee and UPPER SECTION #%d is available, that section has been automatically filled with %d points.\n", required_upper_section + 1, scoring[required_upper_section][0]);
                    skip_scoring = 'Y';
                }
                // player can score in the lower section (if the upper section box's has been already filled)
                else if (scoring[required_upper_section][1] == 1 && lower_section_filled == 'N'){
                    printf("\nSince you have scored another Yahtzee and but UPPER SECTION #%d is NOT available, you may use the additional Yahtzee to score any LOWER SECTIONS.", required_upper_section + 1);
                    lower_section_only = 'Y';
                }
            }
        }

        // if player scores yahtzee
        if((scoring[11][2] == 50) && (amount_yahtzee == 0)){
            printf("\n\nCongratulations! You scored a Yahtzee!");
            amount_yahtzee += 1;
        }

        // score selection
        if(skip_scoring == 'N'){
            if(lower_section_only == 'N'){
                printf("\n\nPlease select where you'd like to score... (1-13)\n");
                scanf(" %d", &score_where);
                while((score_where <= 0) || (score_where >= 14) || (scoring[score_where - 1][1] == 1)){
                    printf("\nInvalid input!");
                    printf("\nPlease select where you'd like to score... (1-13)\n");
                    scanf(" %d", &score_where);
                }
            }
            else if(lower_section_only == 'Y'){
                printf("\n\nPlease select where you'd like to score... (6-13)\n");
                scanf(" %d", &score_where);
                while((score_where <= 5) || (score_where >= 14) || (scoring[score_where - 1][1] == 1)){
                    printf("\nInvalid input!");
                    printf("\nPlease select where you'd like to score... (6-13)\n");
                    scanf(" %d", &score_where);
                }
            }
            scoring[score_where - 1][0] = scoring[score_where - 1][2];
            scoring[score_where - 1][1] = 1;
        }

        for (int i = 0; i <= 12; i++) {
            scoring[i][2] = 0;
        }

        if(scoring[11][0] == 50) yahtzee_achieved = 'Y';
        
        // current scores
        printf("\nCurrent Score:");
        printf("\nUpper Section");

        printf("\n1. Aces            | %d", scoring[0][0]);
        if (scoring[0][1] != 1) printf(" (Unscored)");
        else if (scoring[0][1] == 1) printf(" (Scored)");

        printf("\n2. Twos            | %d", scoring[1][0]);
        if (scoring[1][1] != 1) printf(" (Unscored)");
        else if (scoring[1][1] == 1) printf(" (Scored)");

        printf("\n3. Threes          | %d", scoring[2][0]);
        if (scoring[2][1] != 1) printf(" (Unscored)");
        else if (scoring[2][1] == 1) printf(" (Scored)");

        printf("\n4. Fours           | %d", scoring[3][0]);
        if (scoring[3][1] != 1) printf(" (Unscored)");
        else if (scoring[3][1] == 1) printf(" (Scored)");

        printf("\n5. Fives           | %d", scoring[4][0]);
        if (scoring[4][1] != 1) printf(" (Unscored)");
        else if (scoring[4][1] == 1) printf(" (Scored)");

        printf("\n6. Sixes           | %d", scoring[5][0]);
        if (scoring[5][1] != 1) printf(" (Unscored)");
        else if (scoring[5][1] == 1) printf(" (Scored)");

        printf("\nLower Section");

        printf("\n7. Three-of-a-Kind | %d", scoring[6][0]);
        if (scoring[6][1] != 1) printf(" (Unscored)");
        else if (scoring[6][1] == 1) printf(" (Scored)");

        printf("\n8. Four-of-a-Kind  | %d", scoring[7][0]);
        if (scoring[7][1] != 1) printf(" (Unscored)");
        else if (scoring[7][1] == 1) printf(" (Scored)");

        printf("\n9. Full House      | %d", scoring[8][0]);
        if (scoring[8][1] != 1) printf(" (Unscored)");
        else if (scoring[8][1] == 1) printf(" (Scored)");

        printf("\n10. Small Straight | %d", scoring[9][0]);
        if (scoring[9][1] != 1) printf(" (Unscored)");
        else if (scoring[9][1] == 1) printf(" (Scored)");

        printf("\n11. Large Straight | %d", scoring[10][0]);
        if (scoring[10][1] != 1) printf(" (Unscored)");
        else if (scoring[10][1] == 1) printf(" (Scored)");

        printf("\n12. Yahtzee        | %d", scoring[11][0]);
        if (scoring[11][1] != 1) printf(" (Unscored)");
        else if (scoring[11][1] == 1) printf(" (Scored)");

        printf("\n13. Chance         | %d", scoring[12][0]);
        if (scoring[12][1] != 1) printf(" (Unscored)");
        else if (scoring[12][1] == 1) printf(" (Scored)");

        if(bonus_achieved == 'N'){
            for (int i = 0; i <= 5; i++) {
                calc_upper += scoring[i][0];
            }
            if(calc_upper < 63){
                points_to_bonus = 63 - calc_upper;
                printf("\n\nYou need get %d more points in the UPPER SECTION to receive a 35-point bonus!", points_to_bonus);
                calc_upper = 0;
            }
            else {
                printf("\n\nCongratulations! You've achieved 63 or more points in the UPPER SECTION and received a 35-point bonus!");
                scoring[13][0] = 35;
                scoring[13][1] = 1;
                bonus_achieved = 'Y';
            }
        }

        counter = 0;
        if(upper_section_filled == 'N'){
            for (int i = 0; i <= 5; i++) {
                if(scoring[i][1] == 1) counter += 1;
            }
            if(counter == 6){
                upper_section_filled = 'Y';
                printf("\n\nThe UPPER SECTION has been completely filled!");
            }
        }

        counter = 0;
        if(lower_section_filled == 'N'){
            for (int i = 6; i <= 12; i++) {
                if(scoring[i][1] == 1) counter += 1;
            }
            if(counter == 7){
                lower_section_filled = 'Y';
                printf("\n\nThe LOWER SECTION has been completely filled!");
            }
        }

        skip_scoring = 'N';
        lower_section_only = 'N';
        turn_count += 1;
        add_reroll = 2;
    }

    printf("\n\nGame had ended!");

    for (int i = 0; i <= 5; i++) {
        upper_total_score += scoring[i][0];
    }

    for (int i = 6; i <= 12; i++) {
        lower_total_score += scoring[i][0];
    }

    for (int i = 0; i <= 14; i++) {
        p_score += scoring[i][0];
    }

    // current scores
    printf("\n\nFinal Score:");
    printf("\nUpper Section");
    printf("\n-------------------- ");
    printf("\n1. Aces             | %d", scoring[0][0]);
    printf("\n2. Twos             | %d", scoring[1][0]);
    printf("\n3. Threes           | %d", scoring[2][0]);
    printf("\n4. Fours            | %d", scoring[3][0]);
    printf("\n5. Fives            | %d", scoring[4][0]);
    printf("\n6. Sixes            | %d", scoring[5][0]);
    printf("\n--------------------|");
    printf("\nTotal Score         | %d", upper_total_score);
    printf("\n--------------------|");
    printf("\nBonus               | %d", scoring[13][0]);
    printf("\n--------------------|");
    printf("\nTotal Upper Section | %d", upper_total_score + scoring[13][0]);
    printf("\n-------------------- \n");

    printf("\nLower Section");
    printf("\n------------------------------------- ");
    printf("\n7. Three-of-a-Kind                   | %d", scoring[6][0]);
    printf("\n8. Four-of-a-Kind                    | %d", scoring[7][0]);
    printf("\n9. Full House                        | %d", scoring[8][0]);
    printf("\n10. Small Straight                   | %d", scoring[9][0]);
    printf("\n11. Large Straight                   | %d", scoring[10][0]);
    printf("\n12. Yahtzee                          | %d", scoring[11][0]);
    printf("\n13. Chance                           | %d", scoring[12][0]);
    printf("\n-------------------------------------|");
    printf("\nYahtzee Bonus (100 per add. Yahtzee) | %d", scoring[14][0]);
    printf("\n-------------------------------------|");
    printf("\nTotal Lower Section                  | %d", lower_total_score + scoring[14][0]);
    printf("\nTotal Upper Section                  | %d", upper_total_score + scoring[13][0]);
    printf("\n-------------------------------------|");
    printf("\nGRAND TOTAL                          | %d", p_score);
    printf("\n------------------------------------- \n");

    return 0;
}