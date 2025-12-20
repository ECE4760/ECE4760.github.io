/**
 * Rachel Lee
 * Zephan Sanghani
 * Kailey Ye
 *
 * PicoChess: ECE4760 Final Project
 */

#ifndef CHESSBOARD_H
#define CHESSBOARD_H

#include <stdint.h>

#define BASE_CHESSBOARD_PIN 0

typedef enum {
  BOARD_SETUP,
  BOARD_IDLE,
  BOARD_OWN_LIFTED,
  BOARD_ENEMY_LIFTED,
  BOARD_BOTH_LIFTED,
  BOARD_INVALID,
  BOARD_COMPUTER,
  BOARD_VERIFY
} board_state_t;

void chessboard_init(void);
void chessboard_scan(void);
int chessboard_check_setup(void);
void chessboard_debug_view(void);
void chessboard_reset_view(void);
void chessboard_start_game(int game_mode);
int chessboard_update(void);
void chessboard_computer_moved(const char *from, const char *to);
board_state_t chessboard_get_state(void);

void status_show(const char *msg);

#endif
