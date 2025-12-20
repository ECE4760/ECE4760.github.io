/**
 * Rachel Lee
 * Zephan Sanghani
 * Kailey Ye
 *
 * PicoChess: ECE4760 Final Project
 */

#include "chess.h"
#include "vga16_graphics_v2.h"
#include "user_interface.h"
#include <stdbool.h>
#include <string.h>

// game state and board configuration
static ChessState game_state = {
  .board = {{0}},
  .current_turn = 1,
  .king_in_check_r = -1,
  .king_in_check_c = -1,
  .king_in_check_color = -1,
  .en_passant_r = -1,
  .en_passant_c = -1
};
static int valid_moves[8][8];

// display constants
static const int tile_size = 56;
static const int ox = 20, oy = 20;
static const char dark = 2, light = 1;

// selection state for piece movement
static Tile held_tile;
static int held_origin_r = -1;
static int held_origin_c = -1;
static bool holding_piece = false;
static bool render_enabled = true;
static int promo_r = -1, promo_c = -1;
static int game_over = 0;

// returns tile background color based on position
static inline char tile_color(int r, int c) {
  return ((r + c) & 1) ? light : dark;
}

// clears valid moves array
static void reset_valid_moves(void) {
  memset(valid_moves, 0, sizeof valid_moves);
}

// resets piece selection state
static void reset_selection_state(void) {
  holding_piece = false;
  held_origin_r = held_origin_c = -1;
  held_tile = (Tile){ EMPTY, 0 };
  reset_valid_moves();
}

static void draw_square_outline(int r, int c, char color);

// draws a single tile with piece if present
static void draw_tile(int r, int c) {
  if (!render_enabled)
    return;

  char col = tile_color(r, c);
  fillRect(ox + c * tile_size, oy + r * tile_size, tile_size, tile_size, col);

  Tile t = game_state.board[r][c];
  if (t.piece != EMPTY)
    drawPiece(t.piece, !t.color, ox + c * tile_size, oy + r * tile_size, 2);
}

// redraws entire board
static void draw_full_board(void) {
  if (!render_enabled)
    return;

  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++)
      draw_tile(r, c);
}

// draws colored border around a square
static void draw_square_outline(int r, int c, char color) {
  if (!render_enabled)
    return;

  int x = ox + c * tile_size;
  int y = oy + r * tile_size;
  drawRect(x, y, tile_size, tile_size, color);
  drawRect(x + 1, y + 1, tile_size - 2, tile_size - 2, color);
}

// highlights selected piece and valid move squares
static void show_selection_art(void) {
  if (!holding_piece)
    return;
  if (!render_enabled)
    return;

  // highlight origin square yellow
  draw_square_outline(held_origin_r, held_origin_c, YELLOW);

  // highlight valid moves cyan, captures magenta
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++)
      if (valid_moves[r][c]) {
        Tile t = game_state.board[r][c];
        char color = (t.piece != EMPTY) ? MAGENTA : CYAN;
        draw_square_outline(r, c, color);
      }
}

// removes selection highlighting from board
static void remove_selection_art(void) {
  if (!holding_piece)
    return;
  if (!render_enabled)
    return;

  draw_square_outline(held_origin_r, held_origin_c, tile_color(held_origin_r, held_origin_c));

  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++)
      if (valid_moves[r][c])
        draw_square_outline(r, c, tile_color(r, c));
}

// clears current piece selection
static void clear_selection(void) {
  remove_selection_art();
  reset_selection_state();
}

// converts algebraic notation to row/col
int coord_to_rc(const char *s, int *r, int *c) {
  if (!s) return 0;
  char file = s[0];
  if (file >= 'A' && file <= 'H')
    file = (char)(file - 'A' + 'a');
  if (file < 'a' || file > 'h') return 0;
  if (s[1] < '1' || s[1] > '8') return 0;

  *c = file - 'a';
  *r = 7 - (s[1] - '1');
  return 1;
}

// sets up initial board position and draws it
void draw_initial_board() {
  game_over = 0;
  game_state.current_turn = 1;

  Piece back_rank[8] = {
    ROOK, KNIGHT, BISHOP, QUEEN,
    KING, BISHOP, KNIGHT, ROOK
  };

  // clear middle rows
  for (int r = 2; r < 6; r++)
    for (int c = 0; c < 8; c++)
      game_state.board[r][c] = (Tile){ EMPTY, 0 };

  // place pieces in starting positions
  for (int c = 0; c < 8; c++) {
    game_state.board[0][c] = (Tile){ back_rank[c], 0 };
    game_state.board[1][c] = (Tile){ PAWN, 0 };
    game_state.board[6][c] = (Tile){ PAWN, 1 };
    game_state.board[7][c] = (Tile){ back_rank[c], 1 };
  }

  // reset game state
  clear_selection();
  game_state.current_turn = 1;
  game_state.king_in_check_color = -1;
  game_state.king_in_check_r = game_state.king_in_check_c = -1;
  game_state.en_passant_r = game_state.en_passant_c = -1;
  draw_full_board();

  if (!render_enabled)
    return;

  // draw file and rank labels
  setTextColor(3);
  setTextSize(1);

  for (int c = 0; c < 8; c++) {
    char s[2] = { 'A' + c, 0 };
    setCursor(ox + c * tile_size + tile_size / 2 - 3, oy + 8 * tile_size + 2);
    writeString(s);
  }

  for (int r = 0; r < 8; r++) {
    char s[2] = { '8' - r, 0 };
    setCursor(ox - 12, oy + r * tile_size + tile_size / 2 - 4);
    writeString(s);
  }
}

// calculates valid moves for piece at given position
void find_valid(int r, int c) {
  reset_valid_moves();

  Tile tile = game_state.board[r][c];
  Piece piece = tile.piece;
  int color = tile.color;

  if (piece == EMPTY)
    return;

  // pawn movement and captures
  if (piece == PAWN) {
    int dir = color ? -1 : 1;
    int nr = r + dir;

    // single step forward
    if ((unsigned)nr < 8 && game_state.board[nr][c].piece == EMPTY) {
      valid_moves[nr][c] = 1;

      // double step from starting row
      if ((color && r == 6) || (!color && r == 1)) {
        int nr2 = r + (dir << 1);
        if (game_state.board[nr2][c].piece == EMPTY)
          valid_moves[nr2][c] = 1;
      }
    }

    // diagonal captures and en passant
    for (int dc = -1; dc <= 1; dc += 2) {
      if ((unsigned)nr < 8 && (unsigned)(c + dc) < 8) {
        int cc = c + dc;
        Tile t = game_state.board[nr][cc];
        valid_moves[nr][cc] = (t.piece != EMPTY && t.color != color);
        if (!valid_moves[nr][cc] && nr == game_state.en_passant_r && cc == game_state.en_passant_c) {
          Tile ep = game_state.board[r][cc];
          if (ep.piece == PAWN && ep.color != color)
            valid_moves[nr][cc] = 1;
        }
      }
    }
  }

  // rook and queen straight line moves
  if (piece == ROOK || piece == QUEEN) {
    // up
    for (int rr = r - 1; rr >= 0; rr--) {
      Tile cap = game_state.board[rr][c];
      valid_moves[rr][c] = 1;
      if (cap.piece != EMPTY) {
        if (cap.color == color)
          valid_moves[rr][c] = 0;
        break;
      }
    }
    // down
    for (int rr = r + 1; rr < 8; rr++) {
      Tile cap = game_state.board[rr][c];
      valid_moves[rr][c] = 1;
      if (cap.piece != EMPTY) {
        if (cap.color == color)
          valid_moves[rr][c] = 0;
        break;
      }
    }
    // left
    for (int cc = c - 1; cc >= 0; cc--) {
      Tile cap = game_state.board[r][cc];
      valid_moves[r][cc] = 1;
      if (cap.piece != EMPTY) {
        if (cap.color == color)
          valid_moves[r][cc] = 0;
        break;
      }
    }
    // right
    for (int cc = c + 1; cc < 8; cc++) {
      Tile cap = game_state.board[r][cc];
      valid_moves[r][cc] = 1;
      if (cap.piece != EMPTY) {
        if (cap.color == color)
          valid_moves[r][cc] = 0;
        break;
      }
    }
  }

  // bishop and queen diagonal moves
  if (piece == BISHOP || piece == QUEEN) {
    for (int rr = r - 1, cc = c - 1; rr >= 0 && cc >= 0; rr--, cc--) {
      Tile cap = game_state.board[rr][cc];
      valid_moves[rr][cc] = 1;
      if (cap.piece != EMPTY) {
        if (cap.color == color)
          valid_moves[rr][cc] = 0;
        break;
      }
    }
    for (int rr = r - 1, cc = c + 1; rr >= 0 && cc < 8; rr--, cc++) {
      Tile cap = game_state.board[rr][cc];
      valid_moves[rr][cc] = 1;
      if (cap.piece != EMPTY) {
        if (cap.color == color)
          valid_moves[rr][cc] = 0;
        break;
      }
    }
    for (int rr = r + 1, cc = c - 1; rr < 8 && cc >= 0; rr++, cc--) {
      Tile cap = game_state.board[rr][cc];
      valid_moves[rr][cc] = 1;
      if (cap.piece != EMPTY) {
        if (cap.color == color)
          valid_moves[rr][cc] = 0;
        break;
      }
    }
    for (int rr = r + 1, cc = c + 1; rr < 8 && cc < 8; rr++, cc++) {
      Tile cap = game_state.board[rr][cc];
      valid_moves[rr][cc] = 1;
      if (cap.piece != EMPTY) {
        if (cap.color == color)
          valid_moves[rr][cc] = 0;
        break;
      }
    }
  }

  // knight l-shaped moves
  if (piece == KNIGHT) {
    static const signed char d[8][2] = {
      {-2,-1},{-2,1},{-1,-2},{-1,2},
      {1,-2},{1,2},{2,-1},{2,1}
    };
    for (int i = 0; i < 8; i++) {
      int rr = r + d[i][0], cc = c + d[i][1];
      if ((unsigned)rr < 8 && (unsigned)cc < 8) {
        Tile t = game_state.board[rr][cc];
        valid_moves[rr][cc] = (t.piece == EMPTY || t.color != color);
      }
    }
  }

  // king single square moves
  if (piece == KING) {
    for (int dr = -1; dr <= 1; dr++)
      for (int dc = -1; dc <= 1; dc++)
        if (dr || dc) {
          int rr = r + dr, cc = c + dc;
          if ((unsigned)rr < 8 && (unsigned)cc < 8) {
            Tile t = game_state.board[rr][cc];
            valid_moves[rr][cc] = (t.piece == EMPTY || t.color != color);
          }
        }
  }
}

// finds position of king of given color
static bool find_king(int color, int *king_r, int *king_c) {
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      if (game_state.board[r][c].piece == KING && game_state.board[r][c].color == color) {
        *king_r = r;
        *king_c = c;
        return true;
      }
    }
  }
  return false;
}

// checks if king of given color is in check
int is_king_in_check(int color) {
  int king_r, king_c;
  if (!find_king(color, &king_r, &king_c)) {
    if (game_state.king_in_check_color == color) {
      game_state.king_in_check_color = -1;
      game_state.king_in_check_r = -1;
      game_state.king_in_check_c = -1;
    }
    return 0;
  }

  // check if any opponent piece can capture king
  int opponent_color = !color;
  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      Tile t = game_state.board[r][c];
      if (t.piece != EMPTY && t.color == opponent_color) {
        find_valid(r, c);
        if (valid_moves[king_r][king_c]) {
          game_state.king_in_check_r = king_r;
          game_state.king_in_check_c = king_c;
          game_state.king_in_check_color = color;
          return 1;
        }
      }
    }
  }

  // clear check state if not in check
  if (game_state.king_in_check_color == color) {
    game_state.king_in_check_color = -1;
    game_state.king_in_check_r = -1;
    game_state.king_in_check_c = -1;
  }

  return 0;
}

// checks if player has any legal moves
static bool has_legal_moves(int color) {
  int in_check = is_king_in_check(color);

  for (int r = 0; r < 8; r++) {
    for (int c = 0; c < 8; c++) {
      Tile t = game_state.board[r][c];
      if (t.piece != EMPTY && t.color == color) {
        find_valid(r, c);
        int saved_valid_moves[8][8];
        for (int rr = 0; rr < 8; rr++)
          for (int cc = 0; cc < 8; cc++)
            saved_valid_moves[rr][cc] = valid_moves[rr][cc];
        
        // try each move and see if it gets out of check
        for (int rr = 0; rr < 8; rr++) {
          for (int cc = 0; cc < 8; cc++) {
            if (saved_valid_moves[rr][cc]) {
              if (!in_check)
                return 1;

              // simulate move
              Tile saved_dest = game_state.board[rr][cc];
              Tile saved_src = game_state.board[r][c];
              game_state.board[rr][cc] = saved_src;
              game_state.board[r][c] = (Tile){ EMPTY, 0 };

              int still_in_check = is_king_in_check(color);

              // restore board
              game_state.board[r][c] = saved_src;
              game_state.board[rr][cc] = saved_dest;

              if (!still_in_check)
                return 1;
            }
          }
        }
      }
    }
  }
  return false;
}

// checks if player is in checkmate
static bool is_checkmate(int color) {
  if (!is_king_in_check(color))
    return false;
  return !has_legal_moves(color);
}

// displays game over message on screen
static void display_game_over_message(const char *headline, const char *subline) {
  if (!render_enabled)
    return;
  if (game_over)
    return;

  game_over = 1;

  int center_x = ox + (tile_size * 4);
  int headline_y = oy + tile_size * 2;

  setTextColor(RED);

  // draw main headline
  setTextSize(3);
  int headline_len = (int)strlen(headline);
  int headline_px = headline_len * 6 * 3;
  setCursor(center_x - headline_px / 2, headline_y);
  writeString((char *)headline);

  // draw subline with winner
  setTextSize(2);
  int sub_len = (int)strlen(subline);
  int sub_px = sub_len * 6 * 2;
  setCursor(center_x - sub_px / 2, headline_y + 32);
  writeString((char *)subline);

  ui_draw_game_over_menu();
}

// selects piece at given coordinate for movement
int pick_piece(const char *from) {
  if (game_over)
    return 0;
  int r, c;

  if (!from || holding_piece)
    return 0;

  if (!coord_to_rc(from, &r, &c))
    return 0;

  Tile tile = game_state.board[r][c];
  if (tile.piece == EMPTY)
    return 0;

  // can only pick your own pieces
  if (tile.color != game_state.current_turn)
    return 0;

  find_valid(r, c);

  // filter out moves that leave king in check
  if (game_state.king_in_check_color == tile.color) {
    int saved_valid_moves[8][8];
    for (int rr = 0; rr < 8; rr++)
      for (int cc = 0; cc < 8; cc++)
        saved_valid_moves[rr][cc] = valid_moves[rr][cc];

    for (int rr = 0; rr < 8; rr++) {
      for (int cc = 0; cc < 8; cc++) {
        if (saved_valid_moves[rr][cc]) {
          Tile saved_dest = game_state.board[rr][cc];
          Tile saved_src = game_state.board[r][c];
          game_state.board[rr][cc] = saved_src;
          game_state.board[r][c] = (Tile){ EMPTY, 0 };

          bool still_in_check = is_king_in_check(tile.color);

          game_state.board[r][c] = saved_src;
          game_state.board[rr][cc] = saved_dest;

          if (still_in_check)
            saved_valid_moves[rr][cc] = 0;
        }
      }
    }
    
    for (int rr = 0; rr < 8; rr++)
      for (int cc = 0; cc < 8; cc++)
        valid_moves[rr][cc] = saved_valid_moves[rr][cc];
  }

  // store selection state
  held_tile = tile;
  held_origin_r = r;
  held_origin_c = c;
  holding_piece = true;

  show_selection_art();

  return 1;
}

// places held piece at given coordinate
PlaceResult place_piece(const char *to) {
  if (game_over)
    return PLACE_INVALID;
  if (!holding_piece || !to)
    return PLACE_INVALID;

  int r, c;
  if (!coord_to_rc(to, &r, &c))
    return PLACE_INVALID;

  bool returning_home = (r == held_origin_r && c == held_origin_c);

  if (!returning_home && !valid_moves[r][c])
    return PLACE_INVALID;

  remove_selection_art();

  PlaceResult result = returning_home ? PLACE_RETURNED : PLACE_MOVED;

  if (returning_home) {
    draw_tile(held_origin_r, held_origin_c);
    reset_selection_state();
  } else {
    // verify move doesnt put own king in check
    Tile saved_dest = game_state.board[r][c];
    game_state.board[held_origin_r][held_origin_c] = (Tile){ EMPTY, 0 };
    game_state.board[r][c] = held_tile;

    int mover_color = held_tile.color;
    bool is_en_passant = (held_tile.piece == PAWN && r == game_state.en_passant_r && c == game_state.en_passant_c);
    int ep_r = is_en_passant ? held_origin_r : -1;
    int ep_c = is_en_passant ? c : -1;
    Tile en_passant_captured = (Tile){ EMPTY, 0 };
    
    // handle en passant capture
    if (is_en_passant) {
      en_passant_captured = game_state.board[ep_r][ep_c];
      game_state.board[ep_r][ep_c] = (Tile){ EMPTY, 0 };
    }
    
    // revert if move leaves king in check
    if (is_king_in_check(mover_color)) {
      game_state.board[held_origin_r][held_origin_c] = held_tile;
      game_state.board[r][c] = saved_dest;
      if (is_en_passant)
        game_state.board[ep_r][ep_c] = en_passant_captured;
      find_valid(held_origin_r, held_origin_c);
      show_selection_art();
      return PLACE_INVALID;
    }

    // update display
    draw_tile(held_origin_r, held_origin_c);
    if (is_en_passant)
      draw_tile(ep_r, ep_c);
    draw_tile(r, c);
    
    // check for pawn promotion
    bool is_promotion = (held_tile.piece == PAWN) &&
                        ((mover_color && r == 0) || (!mover_color && r == 7));
    
    // record move in history
    if (chess_is_render_enabled()) {
      int captured_piece = (saved_dest.piece != EMPTY) ? saved_dest.piece : (is_en_passant ? PAWN : EMPTY);
      add_move(held_origin_r, held_origin_c, r, c, held_tile.piece, mover_color, captured_piece);
      if (is_promotion) {
        promo_r = r;
        promo_c = c;
        show_promotion_menu(mover_color);
      }
    }
    
    // update en passant state
    game_state.en_passant_r = game_state.en_passant_c = -1;
    if (held_tile.piece == PAWN) {
      int dr = r - held_origin_r;
      if (dr == 2 || dr == -2) {
        game_state.en_passant_r = (held_origin_r + r) / 2;
        game_state.en_passant_c = c;
      }
    }
    
    // switch turns and update timers
    game_state.current_turn = !game_state.current_turn;
    update_timers(game_state.current_turn);
    
    // update check state for both players
    is_king_in_check(game_state.current_turn);
    is_king_in_check(!game_state.current_turn);
    
    // redraw kings to show check highlighting
    int white_king_r, white_king_c, black_king_r, black_king_c;
    if (find_king(1, &white_king_r, &white_king_c))
      draw_tile(white_king_r, white_king_c);
    if (find_king(0, &black_king_r, &black_king_c))
      draw_tile(black_king_r, black_king_c);
    
    // check for checkmate
    int opponent_color = !mover_color;
    if (is_checkmate(opponent_color)) {
      const char *winner_text = mover_color ? "White wins" : "Black wins";
      display_game_over_message("CHECKMATE", winner_text);
    }
    
    reset_selection_state();
  }

  return result;
}

// cancels current piece selection
int cancel_selection(void) {
  if (!holding_piece)
    return 0;

  remove_selection_art();
  draw_tile(held_origin_r, held_origin_c);
  reset_selection_state();
  return 1;
}

// moves piece from one coordinate to another
int move_piece(const char *from, const char *to) {
  if (game_over)
    return 0;
  if (!pick_piece(from))
    return 0;

  PlaceResult result = place_piece(to);
  if (result == PLACE_INVALID) {
    cancel_selection();
    return 0;
  }

  return (result == PLACE_MOVED);
}

// returns tile at given board position
Tile get_board_tile(int row, int col) {
  if (row < 0 || row >= 8 || col < 0 || col >= 8)
    return (Tile){ EMPTY, 0 };
  return game_state.board[row][col];
}

// returns current player turn (1=white, 0=black)
int get_current_turn(void) {
  return game_state.current_turn;
}

// returns pointer to board array
const Tile (*get_board_state(void))[8] {
  return (const Tile (*)[8])game_state.board;
}

// converts row/col to algebraic notation
void rc_to_coord(int r, int c, char *coord) {
  if (r < 0 || r >= 8 || c < 0 || c >= 8 || !coord) {
    if (coord) coord[0] = '\0';
    return;
  }
  coord[0] = 'a' + c;
  coord[1] = '1' + (7 - r);
  coord[2] = '\0';
}

// returns if move to position is valid
int get_valid_move(int r, int c) {
  if (r < 0 || r >= 8 || c < 0 || c >= 8)
    return 0;
  return valid_moves[r][c];
}

// manually sets valid move flag for position
void set_valid_move(int r, int c, int valid) {
  if (r >= 0 && r < 8 && c >= 0 && c < 8)
    valid_moves[r][c] = valid;
}

// checks if there is a pending pawn promotion
static int chess_has_pending_promotion(void) {
  return promo_r >= 0 && promo_c >= 0;
}

// applies pawn promotion to selected piece type
void chess_apply_promotion(int piece) {
  if (!chess_has_pending_promotion())
    return;
  if (piece != ROOK && piece != KNIGHT && piece != BISHOP && piece != QUEEN)
    piece = QUEEN;
  game_state.board[promo_r][promo_c].piece = (Piece)piece;
  draw_tile(promo_r, promo_c);
  promo_r = promo_c = -1;
}

// saves current game state to buffer
void chess_save_state(ChessState *state) {
  if (!state)
    return;
  memcpy(state, &game_state, sizeof(ChessState));
}

// restores game state from buffer
void chess_restore_state(const ChessState *state) {
  if (!state)
    return;
  memcpy(&game_state, state, sizeof(ChessState));
  reset_selection_state();
}

// enables or disables rendering
void chess_set_render_enabled(int enabled) {
  render_enabled = (enabled != 0);
}

// returns if rendering is enabled
int chess_is_render_enabled(void) {
  return render_enabled ? 1 : 0;
}

// returns if game has ended
int chess_is_game_over(void) {
  return game_over;
}

// handles timer expiration game over
void chess_on_time_expired(int loser) {
  if (game_over)
    return;
  const char *winner_text = (loser == 1) ? "Black wins" : "White wins";
  display_game_over_message("TIME OUT", winner_text);
}
