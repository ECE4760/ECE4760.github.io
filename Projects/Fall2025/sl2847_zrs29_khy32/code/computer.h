/**
 * Rachel Lee
 * Zephan Sanghani
 * Kailey Ye
 *
 * PicoChess: ECE4760 Final Project
 */

#ifndef COMPUTER_H
#define COMPUTER_H

typedef struct {
  int from_r, from_c;
  int to_r, to_c;
  char from_coord[3];
  char to_coord[3];
} Move;

void init_computer(int color);
int is_computer_turn(void);
int computer_make_move(void);
void computer_get_last_move(char *from, char *to);

#endif
