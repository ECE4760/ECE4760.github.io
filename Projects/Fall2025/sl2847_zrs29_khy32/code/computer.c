/**
 * Rachel Lee
 * Zephan Sanghani
 * Kailey Ye
 *
 * PicoChess: ECE4760 Final Project
 */

#include "computer.h"
#include "chess.h"
#include "chessboard.h"
#include "vga16_graphics_v2.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "pico/stdlib.h"

// search configuration
static int computer_color = -1;
static uint64_t max_time_per_move = 2000000;
static int search_timeout = 0;
static uint64_t search_start_time = 0;
static uint32_t search_nodes = 0;
static char last_from[3] = "", last_to[3] = "";

// piece values for material evaluation
static const int piece_values[6] = { 1, 5, 3, 3, 9, 100 };

// positional bonus tables for each piece type
static const int pawn_table[8][8] = {
  {  0,  0,  0,  0,  0,  0,  0,  0},
  { 50, 50, 50, 50, 50, 50, 50, 50},
  { 10, 10, 20, 30, 30, 20, 10, 10},
  {  5,  5, 10, 25, 25, 10,  5,  5},
  {  0,  0,  0, 20, 20,  0,  0,  0},
  {  5, -5,-10,  0,  0,-10, -5,  5},
  {  5, 10, 10,-20,-20, 10, 10,  5},
  {  0,  0,  0,  0,  0,  0,  0,  0}
};

static const int knight_table[8][8] = {
  {-50,-40,-30,-30,-30,-30,-40,-50},
  {-40,-20,  0,  0,  0,  0,-20,-40},
  {-30,  0, 10, 15, 15, 10,  0,-30},
  {-30,  5, 15, 20, 20, 15,  5,-30},
  {-30,  0, 15, 20, 20, 15,  0,-30},
  {-30,  5, 10, 15, 15, 10,  5,-30},
  {-40,-20,  0,  5,  5,  0,-20,-40},
  {-50,-40,-20,-30,-30,-20,-40,-50}
};

static const int bishop_table[8][8] = {
  {-20,-10,-10,-10,-10,-10,-10,-20},
  {-10,  0,  0,  0,  0,  0,  0,-10},
  {-10,  0,  5, 10, 10,  5,  0,-10},
  {-10,  5,  5, 10, 10,  5,  5,-10},
  {-10,  0, 10, 10, 10, 10,  0,-10},
  {-10, 10, 10, 10, 10, 10, 10,-10},
  {-10,  5,  0,  0,  0,  0,  5,-10},
  {-20,-10,-10,-10,-10,-10,-10,-20}
};

static const int rook_table[8][8] = {
  {  0,  0,  0,  0,  0,  0,  0,  0},
  {  5, 10, 10, 10, 10, 10, 10,  5},
  {-5,  0,  0,  0,  0,  0,  0, -5},
  {-5,  0,  0,  0,  0,  0,  0, -5},
  {-5,  0,  0,  0,  0,  0,  0, -5},
  {-5,  0,  0,  0,  0,  0,  0, -5},
  {-5,  0,  0,  0,  0,  0,  0, -5},
  {  0,  0,  0,  5,  5,  0,  0,  0}
};

static const int queen_table[8][8] = {
  {-20,-10,-10, -5, -5,-10,-10,-20},
  {-10,  0,  0,  0,  0,  0,  0,-10},
  {-10,  0,  5,  5,  5,  5,  0,-10},
  { -5,  0,  5,  5,  5,  5,  0, -5},
  {  0,  0,  5,  5,  5,  5,  0, -5},
  {-10,  5,  5,  5,  5,  5,  0,-10},
  {-10,  0,  5,  0,  0,  0,  0,-10},
  {-20,-10,-10, -5, -5,-10,-10,-20}
};

static const int king_table[8][8] = {
  {-30,-40,-40,-50,-50,-40,-40,-30},
  {-30,-40,-40,-50,-50,-40,-40,-30},
  {-30,-40,-40,-50,-50,-40,-40,-30},
  {-30,-40,-40,-50,-50,-40,-40,-30},
  {-20,-30,-30,-40,-40,-30,-30,-20},
  {-10,-20,-20,-20,-20,-20,-20,-10},
  { 20, 20,  0,  0,  0,  0, 20, 20},
  { 20, 30, 10,  0,  0, 10, 30, 20}
};

// sets computer player color
void init_computer(int color) {
  computer_color = color;
}

// checks if its computer's turn
int is_computer_turn(void) {
  if (computer_color < 0) return 0;
  return (get_current_turn() == computer_color);
}

// calculates material and positional score difference
static void evaluate_material_position(int *material_diff, int *position_diff) {
  int white_material = 0, black_material = 0;
  int white_position = 0, black_position = 0;
  const Tile (*board)[8] = get_board_state();
  
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      Tile tile = board[r][c];
      if (tile.piece == EMPTY) continue;

      // sum material value
      int value = piece_values[tile.piece];
      if (tile.color) white_material += value;
      else black_material += value;

      // lookup positional bonus
      int pos_value = 0;
      int table_r = tile.color ? r : 7 - r;
      switch (tile.piece) {
        case PAWN:   pos_value = pawn_table[table_r][c]; break;
        case KNIGHT: pos_value = knight_table[table_r][c]; break;
        case BISHOP: pos_value = bishop_table[table_r][c]; break;
        case ROOK:   pos_value = rook_table[table_r][c]; break;
        case QUEEN:  pos_value = queen_table[table_r][c]; break;
        case KING:   pos_value = king_table[table_r][c]; break;
        default: break;
      }

      if (tile.color) white_position += pos_value;
      else black_position += pos_value;
    }
  }

  *material_diff = white_material - black_material;
  *position_diff = white_position - black_position;
}

// attack maps for mobility evaluation
static int attacked_by_white[8][8] = {{0}};
static int attacked_by_black[8][8] = {{0}};

// builds attack maps for both colors
static void calculate_attacked_squares(void) {
  const Tile (*board)[8] = get_board_state();
  
  // clear attack maps
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      attacked_by_white[r][c] = 0;
      attacked_by_black[r][c] = 0;
    }
  }
  
  // mark squares each piece attacks
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      Tile t = board[r][c];
      if (t.piece == EMPTY) continue;
      
      find_valid(r, c);
      
      for (int rr = 0; rr < 8; rr++) {
        for (int cc = 0; cc < 8; cc++) {
          if (get_valid_move(rr, cc)) {
            if (t.color) attacked_by_white[rr][cc] = 1;
            else attacked_by_black[rr][cc] = 1;
          }
        }
      }
    }
  }
}

// evaluates mobility and threat scores
static int evaluate_mobility(void) {
  calculate_attacked_squares();
  
  int white_mobility = 0, black_mobility = 0;
  int white_threats = 0, black_threats = 0;
  const Tile (*board)[8] = get_board_state();
  
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      Tile tile = board[r][c];
      if (tile.piece == EMPTY || tile.piece == KING) continue;

      find_valid(r, c);
      int piece_weight = piece_values[tile.piece];
      int safe_moves = 0;
      int threats = 0;
      
      // count safe moves and threats
      for (int rr = 0; rr < 8; rr++) {
        for (int cc = 0; cc < 8; cc++) {
          if (get_valid_move(rr, cc)) {
            int is_attacked = tile.color ? attacked_by_black[rr][cc] : attacked_by_white[rr][cc];
            if (!is_attacked) safe_moves++;
            
            Tile target = board[rr][cc];
            if (target.piece != EMPTY && target.color != tile.color)
              threats += piece_values[target.piece];
          }
        }
      }
      
      int mobility_score = safe_moves * piece_weight;
      if (tile.color) {
        white_mobility += mobility_score;
        white_threats += threats;
      } else {
        black_mobility += mobility_score;
        black_threats += threats;
      }
    }
  }

  int mobility_diff = white_mobility - black_mobility;
  int threat_diff = white_threats - black_threats;
  
  return computer_color ? (mobility_diff + threat_diff * 2) : -(mobility_diff + threat_diff * 2);
}

// returns total board evaluation score
static int evaluate_board(void) {
  int material_diff, position_diff;
  evaluate_material_position(&material_diff, &position_diff);
  int material = computer_color ? material_diff : -material_diff;
  int position = computer_color ? position_diff : -position_diff;
  int mobility = evaluate_mobility();
  return material * 100 + position + mobility * 10;
}

// generates all legal moves for color
static int generate_moves(int color, Move *moves) {
  int move_count = 0;
  const Tile (*board)[8] = get_board_state();

  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      Tile tile = board[r][c];
      if (tile.piece == EMPTY || tile.color != color) continue;

      find_valid(r, c);

      // add all valid destination squares
      for (int to_r = 0; to_r < 8; to_r++) {
        for (int to_c = 0; to_c < 8; to_c++) {
          if (get_valid_move(to_r, to_c)) {
            moves[move_count].from_r = r;
            moves[move_count].from_c = c;
            moves[move_count].to_r = to_r;
            moves[move_count].to_c = to_c;
            rc_to_coord(r, c, moves[move_count].from_coord);
            rc_to_coord(to_r, to_c, moves[move_count].to_coord);
            move_count++;
          }
        }
      }
    }
  }

  return move_count;
}

// calculates move ordering score (captures first)
static int move_score(const Move *move) {
  const Tile (*board)[8] = get_board_state();
  Tile from_tile = board[move->from_r][move->from_c];
  Tile to_tile = board[move->to_r][move->to_c];

  int score = 0;

  // prioritize captures by victim value
  if (to_tile.piece != EMPTY && to_tile.color != from_tile.color) {
    int victim_value = piece_values[to_tile.piece];
    int attacker_value = piece_values[from_tile.piece];
    score = 1000 * victim_value - attacker_value;
  } else {
    score = piece_values[from_tile.piece];
  }

  return score;
}

// sorts moves by score for better pruning
static void order_moves(Move *moves, int move_count) {
  for (int i = 1; i < move_count; i++) {
    Move key = moves[i];
    int key_score = move_score(&key);
    int j = i - 1;

    while (j >= 0 && move_score(&moves[j]) < key_score) {
      moves[j + 1] = moves[j];
      j--;
    }
    moves[j + 1] = key;
  }
}

// makes move and saves state for search
static int make_search_move(const Move *move, ChessState *saved_state) {
  if (!move || !saved_state) return 0;

  chess_save_state(saved_state);
  if (!move_piece(move->from_coord, move->to_coord)) return 0;

  search_nodes++;
  return 1;
}

static int is_time_expired(void);

////////////////// BEGIN AI-GENERATED CODE //////////////////

// searches captures until position is quiet
static int quiescence_search(int alpha, int beta, int is_computer_turn) {
  if (is_time_expired()) return evaluate_board();
  
  int current_eval = evaluate_board();
  
  // standing pat check
  if (is_computer_turn) {
    if (current_eval >= beta) return beta;
    if (current_eval > alpha) alpha = current_eval;
  } else {
    if (current_eval <= alpha) return alpha;
    if (current_eval < beta) beta = current_eval;
  }

  // generate and filter captures only
  int current_color = is_computer_turn ? computer_color : !computer_color;
  Move moves[256];
  int move_count = generate_moves(current_color, moves);
  
  int capture_count = 0;
  Move captures[64];
  const Tile (*board)[8] = get_board_state();
  
  for (int i = 0; i < move_count; i++) {
    Tile to_tile = board[moves[i].to_r][moves[i].to_c];
    if (to_tile.piece != EMPTY && to_tile.color != current_color)
      captures[capture_count++] = moves[i];
  }
  
  if (capture_count == 0) return current_eval;
  
  order_moves(captures, capture_count);

  // search captures with alpha-beta
  if (is_computer_turn) {
    int best_eval = current_eval;
    
    for (int i = 0; i < capture_count; i++) {
      if (is_time_expired()) break;
      
      ChessState saved_state;
      if (!make_search_move(&captures[i], &saved_state)) continue;

      int eval = quiescence_search(alpha, beta, 0);
      chess_restore_state(&saved_state);
      
      best_eval = (eval > best_eval) ? eval : best_eval;
      alpha = (alpha > eval) ? alpha : eval;
      
      if (beta <= alpha) break;
    }
    
    return best_eval;
  } else {
    int best_eval = current_eval;
    
    for (int i = 0; i < capture_count; i++) {
      if (is_time_expired()) break;
      
      ChessState saved_state;
      if (!make_search_move(&captures[i], &saved_state)) continue;

      int eval = quiescence_search(alpha, beta, 1);
      chess_restore_state(&saved_state);
      
      best_eval = (eval < best_eval) ? eval : best_eval;
      beta = (beta < eval) ? beta : eval;
      
      if (beta <= alpha) break;
    }
    
    return best_eval;
  }
}

// minimax search with alpha-beta pruning
static int minimax_alphabeta(int depth, int alpha, int beta, int is_computer_turn) {
  if (is_time_expired()) return 0;

  // leaf node - do quiescence search
  if (depth == 0) return quiescence_search(alpha, beta, is_computer_turn);

  int current_color = is_computer_turn ? computer_color : !computer_color;
  Move moves[256];
  int move_count = generate_moves(current_color, moves);

  // no moves - checkmate or stalemate
  if (move_count == 0) {
    if (is_king_in_check(current_color))
      return is_computer_turn ? -999999 : 999999;
    return 0;
  }

  order_moves(moves, move_count);

  // maximizing player
  if (is_computer_turn) {
    int best_eval = -999999;
    
    for (int i = 0; i < move_count; i++) {
      ChessState saved_state;
      if (!make_search_move(&moves[i], &saved_state)) continue;

      int eval = minimax_alphabeta(depth - 1, alpha, beta, 0);
      chess_restore_state(&saved_state);
      
      best_eval = (eval > best_eval) ? eval : best_eval;
      alpha = (alpha > eval) ? alpha : eval;
      
      if (beta <= alpha) break;
    }
    
    return best_eval;
  } else {
    // minimizing player
    int best_eval = 999999;
    
    for (int i = 0; i < move_count; i++) {
      ChessState saved_state;
      if (!make_search_move(&moves[i], &saved_state)) continue;

      int eval = minimax_alphabeta(depth - 1, alpha, beta, 1);
      chess_restore_state(&saved_state);
      
      best_eval = (eval < best_eval) ? eval : best_eval;
      beta = (beta < eval) ? beta : eval;
      
      if (beta <= alpha) break;
    }
    
    return best_eval;
  }
}

/////////////////// END AI-GENERATED CODE ///////////////////

// checks if search time limit exceeded
static int is_time_expired(void) {
  if (search_timeout) return 1;
  
  uint64_t current_time = time_us_64();
  if (current_time - search_start_time > max_time_per_move) {
    search_timeout = 1;
    return 1;
  }
  return 0;
}

////////////////// BEGIN AI-GENERATED CODE //////////////////

// searches with increasing depth until time runs out
static int iterative_deepening_search(Move *best_move) {
  Move moves[256];
  int move_count = generate_moves(computer_color, moves);

  if (move_count == 0) return 0;

  ChessState initial_state;
  chess_save_state(&initial_state);

  // start with first move as default
  *best_move = moves[0];
  int best_eval = -999999;
  search_nodes = 0;

  search_timeout = 0;
  search_start_time = time_us_64();

  // increase depth until timeout
  for (int depth = 1; ; depth++) {
    if (is_time_expired()) break;

    chess_restore_state(&initial_state);

    int current_eval = -999999;
    int current_best_idx = 0;

    order_moves(moves, move_count);

    // search each move at current depth
    for (int i = 0; i < move_count; i++) {
      if (is_time_expired()) break;

      ChessState saved_state;
      if (!make_search_move(&moves[i], &saved_state)) continue;

      int eval = minimax_alphabeta(depth - 1, -999999, 999999, 0);
      chess_restore_state(&saved_state);

      if (eval > current_eval) {
        current_eval = eval;
        current_best_idx = i;
      }
    }

    // only update best if completed full depth
    if (!is_time_expired() && current_eval > best_eval) {
      best_eval = current_eval;
      *best_move = moves[current_best_idx];
    }

    if (is_time_expired()) break;
  }

  chess_restore_state(&initial_state);
  return 1;
}

/////////////////// END AI-GENERATED CODE ///////////////////

// displays search statistics
static void show_search_info(uint32_t nodes) {
  char buf[32];
  sprintf(buf, "%lu paths searched", (unsigned long)nodes);
  status_show(buf);
}

// makes best move for computer player
int computer_make_move(void) {
  if (computer_color < 0) return 0;
  if (get_current_turn() != computer_color) return 0;

  // disable rendering during search
  int render_was_enabled = chess_is_render_enabled();
  chess_set_render_enabled(0);

  Move best_move;
  if (!iterative_deepening_search(&best_move)) {
    chess_set_render_enabled(render_was_enabled);
    return 0;
  }

  chess_set_render_enabled(render_was_enabled);

  // execute the move
  if (move_piece(best_move.from_coord, best_move.to_coord)) {
    strcpy(last_from, best_move.from_coord);
    strcpy(last_to, best_move.to_coord);
    show_search_info(search_nodes);
    return 1;
  }

  return 0;
}

// returns last move made by computer
void computer_get_last_move(char *from, char *to) {
  if (from) strcpy(from, last_from);
  if (to) strcpy(to, last_to);
}
