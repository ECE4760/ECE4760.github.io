/**
 * Rachel Lee
 * Zephan Sanghani
 * Kailey Ye
 *
 * PicoChess: ECE4760 Final Project
 */

#ifndef CHESS_H
#define CHESS_H

typedef enum {
  EMPTY = -1,
  PAWN  = 0,
  ROOK  = 1,
  KNIGHT= 2,
  BISHOP= 3,
  QUEEN = 4,
  KING  = 5
} Piece;

typedef enum {
  PLACE_INVALID  = 0,
  PLACE_MOVED    = 1,
  PLACE_RETURNED = 2
} PlaceResult;

typedef struct {
  Piece piece;
  int color;
} Tile;

typedef struct {
  Tile board[8][8];
  int current_turn;
  int king_in_check_r;
  int king_in_check_c;
  int king_in_check_color;
  int en_passant_r;
  int en_passant_c;
} ChessState;

void draw_initial_board(void);
int  pick_piece(const char *from);
PlaceResult place_piece(const char *to);
int  cancel_selection(void);
int  move_piece(const char *from, const char *to);

Tile get_board_tile(int row, int col);
int  get_current_turn(void);
const Tile (*get_board_state(void))[8];

int coord_to_rc(const char *s, int *r, int *c);
void rc_to_coord(int r, int c, char *coord);

void find_valid(int r, int c);
int get_valid_move(int r, int c);
void set_valid_move(int r, int c, int valid);
int is_king_in_check(int color);

void chess_save_state(ChessState *state);
void chess_restore_state(const ChessState *state);

void chess_set_render_enabled(int enabled);
int  chess_is_render_enabled(void);

void chess_apply_promotion(int piece);

int  chess_is_game_over(void);
void chess_on_time_expired(int loser);

#endif
