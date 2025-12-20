/**
 * Rachel Lee
 * Zephan Sanghani
 * Kailey Ye
 *
 * PicoChess: ECE4760 Final Project
 */

#include "chessboard.h"
#include "chess.h"
#include "computer.h"
#include "vga16_graphics_v2.h"
#include "pico/stdlib.h"
#include <string.h>

// reed switch matrix state
static uint8_t occupancy[8][8];
static uint8_t expected[8][8];

// board tracking state
static board_state_t state = BOARD_SETUP;
static int mode = 2;
static int own_r = -1, own_c = -1;
static int enemy_r = -1, enemy_c = -1;
static int comp_fr = -1, comp_fc = -1;
static int comp_tr = -1, comp_tc = -1;

// display constants
static const int ox = 20, oy = 20, tile = 56;
static const int stat_x = 480, stat_y = 464;
static uint8_t border_set[8][8];

// flips row for physical board orientation
static inline int flip_r(int r) { return 7 - r; }

// gets occupancy at logical board position
static inline int get_occ(int r, int c) { return occupancy[flip_r(r)][c]; }

// displays status message on screen
void status_show(const char *msg) {
  fillRect(stat_x, stat_y, 160, 16, BLACK);
  setTextColor(WHITE); setTextSize(1);
  setCursor(stat_x, stat_y);
  writeString((char*)msg);
}

// draws colored border around square
static void draw_border(int r, int c, char col, int set) {
  int x = ox + c * tile, y = oy + r * tile;
  drawRect(x, y, tile, tile, col);
  drawRect(x+1, y+1, tile-2, tile-2, col);
  border_set[r][c] = set;
}

// clears border from square
static void clear_border(int r, int c) {
  if (!border_set[r][c]) return;
  draw_border(r, c, ((r+c)&1) ? 1 : 2, 0);
}

// clears all borders from board
static void clear_borders(void) {
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++)
      if (border_set[r][c]) clear_border(r, c);
}

// highlights squares where pieces are missing or extra
static void show_errors(void) {
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++) {
      int exp = expected[r][c], act = get_occ(r, c);
      if (exp && !act) draw_border(r, c, RED, 1);
      else if (!exp && act) draw_border(r, c, YELLOW, 1);
    }
}

// displays current turn and check status
static void show_turn(void) {
  int t = get_current_turn(), chk = is_king_in_check(t);
  if (mode == 1) status_show(chk ? "CHECK - your turn" : "your turn");
  else if (chk) status_show(t ? "CHECK - white" : "CHECK - black");
  else status_show(t ? "white's turn" : "black's turn");
}

// checks if physical board matches expected state
static int board_ok(void) {
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++)
      if (expected[r][c] != get_occ(r, c)) return 0;
  return 1;
}

// checks if current player can capture piece at position
static int can_capture(int r, int c) {
  int turn = get_current_turn();
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++) {
      Tile t = get_board_tile(i, j);
      if (t.piece != EMPTY && t.color == turn) {
        find_valid(i, j);
        if (get_valid_move(r, c)) return 1;
      }
    }
  return 0;
}

// finds a square where expected piece was lifted
static int find_lifted(int *r, int *c, int excl_r, int excl_c) {
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++)
      if ((i != excl_r || j != excl_c) && expected[i][j] && !get_occ(i, j)) {
        *r = i; *c = j; return 1;
      }
  return 0;
}

// finds a square where unexpected piece was placed
static int find_placed(int *r, int *c) {
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++)
      if (!expected[i][j] && get_occ(i, j)) {
        *r = i; *c = j; return 1;
      }
  return 0;
}

// finds valid empty square where piece was placed
static int find_empty_placement(int *r, int *c, int excl_r, int excl_c) {
  for (int i = 0; i < 8; i++)
    for (int j = 0; j < 8; j++) {
      if (i == excl_r && j == excl_c) continue;
      if (!get_valid_move(i, j)) continue;
      if (expected[i][j]) continue;
      if (!get_occ(i, j)) continue;
      *r = i; *c = j; return 1;
    }
  return 0;
}

// clears move tracking and borders
static void reset_move(void) {
  if (own_r >= 0) clear_border(own_r, own_c);
  if (enemy_r >= 0) clear_border(enemy_r, enemy_c);
  own_r = own_c = enemy_r = enemy_c = -1;
}

// transitions to idle state waiting for move
static void go_idle(void) {
  reset_move();
  show_turn();
  state = BOARD_IDLE;
}

// transitions to invalid state with error message
static void go_invalid(const char *msg) {
  status_show(msg);
  show_errors();
  state = BOARD_INVALID;
}

// completes a move and updates expected state
static void complete_move(int to_r, int to_c) {
  char coord[3];
  rc_to_coord(to_r, to_c, coord);
  set_valid_move(to_r, to_c, 1);
  
  if (place_piece(coord) == PLACE_MOVED) {
    // update expected board state
    expected[own_r][own_c] = 0;
    if (enemy_r >= 0) expected[enemy_r][enemy_c] = 0;
    expected[to_r][to_c] = 1;
    reset_move();
    
    // check if computer should move next
    if (mode == 1 && is_computer_turn()) {
      status_show("computer thinking...");
      state = BOARD_COMPUTER;
    } else {
      go_idle();
    }
  } else {
    cancel_selection();
    go_invalid("illegal move");
  }
}

// initializes expected board to starting position
static void init_expected(void) {
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++)
      expected[r][c] = (r <= 1 || r >= 6) ? 1 : 0;
}

// initializes gpio for reed switch matrix
void chessboard_init(void) {
  gpio_init_mask(0xFFFF << BASE_CHESSBOARD_PIN);
  gpio_set_dir_out_masked(0xFF << BASE_CHESSBOARD_PIN);
  gpio_put_masked(0xFF << BASE_CHESSBOARD_PIN, 0);
  for (int i = 8; i < 16; i++) gpio_pull_down(BASE_CHESSBOARD_PIN + i);
  memset(occupancy, 0, sizeof(occupancy));
  memset(expected, 0, sizeof(expected));
  memset(border_set, 0, sizeof(border_set));
}

// scans reed switch matrix for piece positions
void chessboard_scan(void) {
  for (int i = 0; i < 8; i++) {
    // enable one row at a time
    gpio_put_masked(0xFF << BASE_CHESSBOARD_PIN, (1 << i) << BASE_CHESSBOARD_PIN);
    sleep_us(10);
    // read all columns
    for (int j = 0; j < 8; j++)
      occupancy[i][j] = gpio_get(BASE_CHESSBOARD_PIN + 8 + j);
  }
  gpio_put_masked(0xFF << BASE_CHESSBOARD_PIN, 0);
}

static uint8_t last_occ[8][8];
static int setup_drawn = 0;

// checks if occupancy changed since last call
static int occ_changed(void) {
  if (memcmp(last_occ, occupancy, sizeof(occupancy)) != 0) {
    memcpy(last_occ, occupancy, sizeof(occupancy));
    return 1;
  }
  return 0;
}

// checks and displays board setup status
int chessboard_check_setup(void) {
  init_expected();
  if (!setup_drawn || occ_changed()) {
    // show green for correct, red for incorrect
    for (int r = 0; r < 8; r++)
      for (int c = 0; c < 8; c++)
        draw_border(r, c, (expected[r][c] == get_occ(r, c)) ? GREEN : RED, 1);
    setup_drawn = 1;
  }
  return board_ok();
}

// displays raw reed switch state for debugging
void chessboard_debug_view(void) {
  if (occ_changed()) {
    for (int r = 0; r < 8; r++)
      for (int c = 0; c < 8; c++)
        draw_border(r, c, get_occ(r, c) ? GREEN : RED, 1);
  }
}

// clears all view state
void chessboard_reset_view(void) {
  clear_borders();
  memset(last_occ, 0xFF, sizeof(last_occ));
  setup_drawn = 0;
}

// starts game with given mode
void chessboard_start_game(int game_mode) {
  mode = game_mode;
  init_expected();
  clear_borders();
  memset(last_occ, 0xFF, sizeof(last_occ));
  setup_drawn = 0;
  own_r = own_c = enemy_r = enemy_c = -1;
  comp_fr = comp_fc = comp_tr = comp_tc = -1;
  
  // check if computer moves first
  if (mode == 1 && is_computer_turn()) {
    state = BOARD_COMPUTER;
    status_show("computer thinking...");
  } else {
    state = BOARD_IDLE;
    show_turn();
  }
}

// returns current board state
board_state_t chessboard_get_state(void) { return state; }

// updates expected state after computer move
void chessboard_computer_moved(const char *from, const char *to) {
  coord_to_rc(from, &comp_fr, &comp_fc);
  coord_to_rc(to, &comp_tr, &comp_tc);
  
  // track if capture occurred
  enemy_r = expected[comp_tr][comp_tc] ? comp_tr : -1;
  enemy_c = expected[comp_tr][comp_tc] ? comp_tc : -1;
  
  // update expected state
  expected[comp_fr][comp_fc] = 0;
  expected[comp_tr][comp_tc] = 1;
  
  // highlight computer move
  draw_border(comp_fr, comp_fc, CYAN, 1);
  draw_border(comp_tr, comp_tc, CYAN, 1);
  status_show("make computer move");
  state = BOARD_VERIFY;
}

// main state machine for physical board interaction
int chessboard_update(void) {
  int r, c;
  char coord[3];
  Tile t;
  
  switch (state) {
    
  case BOARD_SETUP:
    break;
    
  case BOARD_IDLE:
    if (board_ok()) break;
    
    // check for lifted piece
    if (!find_lifted(&r, &c, -1, -1)) {
      if (find_placed(&r, &c)) go_invalid("remove extra piece");
      break;
    }
    
    t = get_board_tile(r, c);
    if (t.piece == EMPTY) { go_invalid("put piece back"); break; }
    
    // own piece lifted - start move
    if (t.color == get_current_turn()) {
      rc_to_coord(r, c, coord);
      if (!pick_piece(coord)) { go_invalid("no valid moves"); break; }
      own_r = r; own_c = c;
      draw_border(own_r, own_c, YELLOW, 1);
      status_show("place piece");
      state = BOARD_OWN_LIFTED;
    } else {
      // enemy piece lifted - check if can be captured
      if (!can_capture(r, c)) { go_invalid("can't take that"); break; }
      enemy_r = r; enemy_c = c;
      draw_border(enemy_r, enemy_c, MAGENTA, 1);
      status_show("pick your piece");
      state = BOARD_ENEMY_LIFTED;
    }
    break;
    
  case BOARD_OWN_LIFTED:
    // piece put back - cancel move
    if (get_occ(own_r, own_c)) {
      rc_to_coord(own_r, own_c, coord);
      place_piece(coord);
      go_idle();
      break;
    }
    
    // check if enemy piece also lifted for capture
    if (find_lifted(&r, &c, own_r, own_c)) {
      t = get_board_tile(r, c);
      if (t.piece != EMPTY && t.color != get_current_turn() && get_valid_move(r, c)) {
        enemy_r = r; enemy_c = c;
        draw_border(enemy_r, enemy_c, MAGENTA, 1);
        status_show("place on capture");
        state = BOARD_BOTH_LIFTED;
        break;
      }
    }
    
    // check if piece placed on valid square
    if (find_empty_placement(&r, &c, own_r, own_c)) {
      complete_move(r, c);
    }
    break;
    
  case BOARD_ENEMY_LIFTED:
    // enemy piece put back - cancel
    if (get_occ(enemy_r, enemy_c)) {
      clear_borders();
      enemy_r = enemy_c = -1;
      go_idle();
      break;
    }
    
    // enemy piece placed elsewhere - invalid
    if (find_placed(&r, &c)) {
      if (occ_changed()) {
        clear_borders();
        show_errors();
        draw_border(enemy_r, enemy_c, MAGENTA, 1);
      }
      status_show("move piece back");
      break;
    }
    
    // check if player piece now lifted
    if (find_lifted(&r, &c, enemy_r, enemy_c)) {
      t = get_board_tile(r, c);
      if (t.piece != EMPTY && t.color == get_current_turn()) {
        rc_to_coord(r, c, coord);
        if (!pick_piece(coord)) {
          clear_border(enemy_r, enemy_c);
          go_invalid("no valid moves");
          break;
        }
        if (!get_valid_move(enemy_r, enemy_c)) {
          cancel_selection();
          clear_border(enemy_r, enemy_c);
          go_invalid("can't capture there");
          break;
        }
        own_r = r; own_c = c;
        draw_border(own_r, own_c, YELLOW, 1);
        status_show("place on capture");
        state = BOARD_BOTH_LIFTED;
      }
    }
    break;
    
  case BOARD_BOTH_LIFTED: {
    int own_back = get_occ(own_r, own_c);
    int piece_on_capture = get_occ(enemy_r, enemy_c);
    
    // own piece put back
    if (own_back) {
      rc_to_coord(own_r, own_c, coord);
      place_piece(coord);
      if (piece_on_capture) {
        go_idle();
      } else {
        clear_border(own_r, own_c);
        own_r = own_c = -1;
        status_show("pick your piece");
        state = BOARD_ENEMY_LIFTED;
      }
      break;
    }
    
    // piece placed on capture square - complete move
    if (piece_on_capture) {
      complete_move(enemy_r, enemy_c);
      break;
    }
    break;
  }
    
  case BOARD_INVALID:
    // wait for board to be corrected
    if (board_ok()) {
      clear_borders();
      own_r = own_c = enemy_r = enemy_c = -1;
      go_idle();
    } else if (occ_changed()) {
      clear_borders();
      show_errors();
    }
    break;
    
  case BOARD_COMPUTER:
    return 1;
    
  case BOARD_VERIFY:
    // waiting for player to execute computer move
    if (enemy_r >= 0 && !get_occ(enemy_r, enemy_c)) {
      enemy_r = enemy_c = -1;
    }
    if (board_ok()) {
      clear_border(comp_fr, comp_fc);
      clear_border(comp_tr, comp_tc);
      comp_fr = comp_fc = comp_tr = comp_tc = -1;
      enemy_r = enemy_c = -1;
      go_idle();
    } else if (occ_changed()) {
      clear_borders();
      show_errors();
      draw_border(comp_fr, comp_fc, CYAN, 1);
      draw_border(comp_tr, comp_tc, CYAN, 1);
    }
    break;
  }
  
  return 0;
}
