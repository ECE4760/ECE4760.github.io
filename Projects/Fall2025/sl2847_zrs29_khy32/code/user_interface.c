/**
 * Rachel Lee
 * Zephan Sanghani
 * Kailey Ye
 *
 * PicoChess: ECE4760 Final Project
 */

////////////////// BEGIN AI-GENERATED CODE //////////////////

#include "user_interface.h"
#include "vga16_graphics_v2.h"
#include "chess.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// keypad gpio mask
#define ROW_MASK (0x7 << KEYPAD_ROW1_PIN)

// display layout constants
#define TEXT_HEIGHT 8
#define MENU_X 480
#define MENU_Y 100
#define MENU_PADDING 8
#define MENU_SPACING 10
#define MENU_BUTTON_WIDTH 100
#define MOVE_HISTORY_X 480
#define MOVE_HISTORY_Y 50
#define MOVE_HISTORY_WIDTH 160
#define MOVE_HISTORY_LINE_HEIGHT 10
#define MOVE_HISTORY_MAX_LINES 28
#define MOVE_HISTORY_MAX_MOVES 200

// keypad row scanning patterns
static const unsigned int scancodes[KEYROWS] = {0x01, 0x02, 0x04};

// menu option strings
static char* mode_options[] = {"1 Player", "2 Player", "Debug"};
static char* color_options[] = {"White", "Black"};
static char* timer_options[] = {"Unlimited", "30m", "10m", "5m"};

// menu state
static menu_state_t menu_state = MENU_MODE_SELECT;
static int menu_game_mode = 1, menu_player_color = 1, menu_timer_minutes = 30, menu_selected_item = 0;
static int start_enabled = 0;

// cycles through game modes
static void toggle_game_mode(void) {
  menu_game_mode = (menu_game_mode % 3) + 1;
}

// toggles between white and black
static void toggle_player_color(void) { menu_player_color = 1 - menu_player_color; }

// handles start button press
static int start_game_action(void) {
  if (!start_enabled) return 0;
  menu_state = MENU_DONE;
  return 1;
}

// menu button definitions
static menu_button_t menu_buttons[] = {
  {BUTTON_TOGGLE, MENU_X, 0, 0, 0, NULL, mode_options, 3, 0, 1, toggle_game_mode, NULL},
  {BUTTON_TOGGLE, MENU_X, 0, 0, 0, NULL, color_options, 2, 0, 0, toggle_player_color, NULL},
  {BUTTON_TOGGLE, MENU_X, 0, 0, 0, NULL, timer_options, 4, 0, 1, NULL, NULL},
  {BUTTON_ACTION, MENU_X, 0, 0, 0, "Start Game", NULL, 0, 0, 1, NULL, start_game_action}
};

#define NUM_MENU_BUTTONS (sizeof(menu_buttons) / sizeof(menu_buttons[0]))

// initializes keypad gpio pins
void init_keypad(void) {
  gpio_init_mask(ROW_MASK | (1 << KEYPAD_COL_PIN));
  gpio_set_dir_out_masked(ROW_MASK);
  gpio_put_masked(ROW_MASK, 0);
  gpio_set_dir(KEYPAD_COL_PIN, GPIO_IN);
  gpio_pull_down(KEYPAD_COL_PIN);
}

// scans keypad and returns pressed key index
int scan_keypad(void) {
  for (int i = 0; i < KEYROWS; i++) {
    gpio_put_masked(ROW_MASK, scancodes[i] << KEYPAD_ROW1_PIN);
    sleep_us(1);
    if (gpio_get(KEYPAD_COL_PIN)) {
      gpio_put_masked(ROW_MASK, 0);
      return i;
    }
    gpio_put_masked(ROW_MASK, 0);
  }
  return -1;
}

// returns display text for button
static char* button_text(const menu_button_t* b) {
  return (b->type == BUTTON_TOGGLE) ? b->options[b->current_option] : b->label;
}

// calculates button dimensions
static void update_button_size(menu_button_t* b) {
  b->width = MENU_BUTTON_WIDTH;
  b->height = TEXT_HEIGHT + (MENU_PADDING * 2);
}

// draws button with selection highlighting
static void draw_button_common(const menu_button_t* b, int sel, char bg_color) {
  fillRect(b->x, b->y, b->width, b->height, BLACK);
  if (!b->is_visible) return;
  
  if (sel) {
    fillRect(b->x, b->y, b->width, b->height, bg_color);
    setTextColor(BLACK);
  } else {
    drawRect(b->x, b->y, b->width, b->height, WHITE);
    setTextColor(WHITE);
  }
  setTextSize(1);
  setCursor(b->x + MENU_PADDING, b->y + MENU_PADDING);
  writeString(b->type == BUTTON_TOGGLE ? button_text(b) : b->label);
}

// draws toggle button with yellow highlight
static void draw_toggle_button(const menu_button_t* b, int sel) {
  draw_button_common(b, sel, YELLOW);
}

// draws action button with green/gray color
static void draw_action_button(const menu_button_t* b, int sel) {
  char color = start_enabled ? GREEN : 3;
  draw_button_common(b, sel && start_enabled, color);
}

// handles toggle button press
static int handle_toggle_button(menu_button_t* b) {
  b->current_option = (b->current_option + 1) % b->num_options;
  if (b->toggle_cb) b->toggle_cb();
  return 0;
}

// handles action button press
static int handle_action_button(menu_button_t* b) {
  return (b->is_visible && b->action_cb) ? b->action_cb() : 0;
}

// timer state variables
static uint64_t white_time_remaining_us = 0;
static uint64_t black_time_remaining_us = 0;
static uint64_t last_timer_update_us = 0;
static int timer_active = 0;
static int current_timer_turn = 1;
static uint64_t last_draw_white_sec = (uint64_t)-1;
static uint64_t last_draw_black_sec = (uint64_t)-1;
static int ui_single_player = 0;

// initializes menu to default state
void init_menu(void) {
  menu_state = MENU_MODE_SELECT;
  menu_game_mode = menu_player_color = 1;
  menu_timer_minutes = 0;
  menu_selected_item = 0;
  for (int i = 0; i < 3; i++) menu_buttons[i].current_option = 0;
  menu_buttons[1].is_visible = 1;
  draw_menu();
}

// draws all menu buttons
void draw_menu(void) {
  if (menu_state == MENU_DONE) {
    fillRect(MENU_X, MENU_Y, MENU_BUTTON_WIDTH, 200, BLACK);
    return;
  }
  
  // update button visibility based on mode
  menu_buttons[1].is_visible = (menu_game_mode == 1);
  menu_buttons[3].is_visible = (menu_game_mode != 3);
  menu_buttons[0].current_option = menu_game_mode - 1;
  menu_buttons[1].current_option = (menu_player_color == 1) ? 0 : 1;
  
  // draw each button
  int y = MENU_Y, vi = 0;
  for (int i = 0; i < NUM_MENU_BUTTONS; i++) {
    update_button_size(&menu_buttons[i]);
    menu_buttons[i].y = y;
    y += menu_buttons[i].height + MENU_SPACING;
    
    int sel = (menu_buttons[i].is_visible && menu_selected_item == vi);
    if (menu_buttons[i].is_visible) vi++;
    (menu_buttons[i].type == BUTTON_TOGGLE) ?
      draw_toggle_button(&menu_buttons[i], sel) :
      draw_action_button(&menu_buttons[i], sel);
  }
}

// processes menu navigation and selection
int process_menu_input(int key_index) {
  if (key_index < 0 || key_index >= NUMKEYS) return 0;
  
  // count visible buttons
  int num_visible = 0;
  for (int i = 0; i < NUM_MENU_BUTTONS; i++)
    if (menu_buttons[i].is_visible) num_visible++;
  if (menu_selected_item >= num_visible) menu_selected_item = num_visible - 1;
  
  // navigate up
  if (key_index == 0) {
    menu_selected_item = (menu_selected_item - 1 + num_visible) % num_visible;
    draw_menu();
  } 
  // navigate down
  else if (key_index == 2) {
    menu_selected_item = (menu_selected_item + 1) % num_visible;
    draw_menu();
  } 
  // select button
  else if (key_index == 1) {
    int vi = 0;
    for (int i = 0; i < NUM_MENU_BUTTONS; i++) {
      if (menu_buttons[i].is_visible && vi++ == menu_selected_item) {
        int result = (menu_buttons[i].type == BUTTON_TOGGLE) ?
          handle_toggle_button(&menu_buttons[i]) : handle_action_button(&menu_buttons[i]);
        static int timer_vals[] = {0, 30, 10, 5};
        if (i == 0) menu_game_mode = menu_buttons[0].current_option + 1, menu_selected_item = 0;
        else if (i == 1) menu_player_color = menu_buttons[1].current_option == 0 ? 1 : 0;
        else if (i == 2) menu_timer_minutes = timer_vals[menu_buttons[2].current_option];
        draw_menu();
        return result;
      }
    }
  }
  return 0;
}

// menu state getters
int is_menu_done(void) { return (menu_state == MENU_DONE); }
int get_menu_game_mode(void) { return menu_game_mode; }
int get_menu_player_color(void) { return menu_player_color; }
int get_menu_timer_minutes(void) { return menu_timer_minutes; }
int is_debug_mode(void) { return menu_game_mode == 3; }
void menu_set_start_enabled(int enabled) { start_enabled = enabled; }

// checks if either player ran out of time
int check_timer_expired(void) {
  if (!timer_active) return -1;
  if (white_time_remaining_us == 0) return 1;
  if (black_time_remaining_us == 0) return 0;
  return -1;
}

// sets single player mode for timer positioning
void ui_set_single_player(int is_single_player) {
  ui_single_player = is_single_player ? 1 : 0;
}

// initializes game timers
void init_timers(int minutes_per_player) {
  timer_active = (minutes_per_player > 0);
  if (timer_active) {
    uint64_t time_us = (uint64_t)minutes_per_player * 60000000;
    white_time_remaining_us = black_time_remaining_us = time_us;
    last_timer_update_us = time_us_64();
    last_draw_white_sec = last_draw_black_sec = (uint64_t)-1;
  }
  current_timer_turn = 1;
  draw_timers();
}

// updates timer on turn change
void update_timers(int current_turn) {
  if (!timer_active || current_timer_turn == current_turn) return;
  
  uint64_t now = time_us_64(), elapsed = now - last_timer_update_us;
  last_timer_update_us = now;
  uint64_t* time_ptr = (current_timer_turn == 1) ? &white_time_remaining_us : &black_time_remaining_us;
  *time_ptr = (*time_ptr > elapsed) ? *time_ptr - elapsed : 0;
  current_timer_turn = current_turn;
}

// updates timer continuously during turn
void update_timers_continuous(void) {
  if (!timer_active) return;
  
  uint64_t now = time_us_64(), elapsed = now - last_timer_update_us;
  if (elapsed == 0) return;
  
  last_timer_update_us = now;
  uint64_t* time_ptr = (current_timer_turn == 1) ? &white_time_remaining_us : &black_time_remaining_us;
  *time_ptr = (*time_ptr > elapsed) ? *time_ptr - elapsed : 0;
  
  // redraw when seconds change
  uint64_t w_sec = white_time_remaining_us / 1000000;
  uint64_t b_sec = black_time_remaining_us / 1000000;
  if (w_sec != last_draw_white_sec || b_sec != last_draw_black_sec)
    draw_timers();
}

// formats microseconds as mm:ss string
static void format_time(uint64_t time_us, char* buf) {
  uint64_t sec = time_us / 1000000;
  uint64_t min = sec / 60;
  sec = sec % 60;
  sprintf(buf, "%02llu:%02llu", min, sec);
}

// draws both player timers
void draw_timers(void) {
  if (!timer_active) return;
  
  // timer box dimensions
  const int timer_padding = 4;
  const int timer_text_width = 5 * 12;
  const int timer_text_height = 16;
  const int timer_box_width = timer_text_width + timer_padding * 2;
  const int timer_box_height = timer_text_height + timer_padding * 2;
  const int timer_x = 478;
  const int top_y = 20;
  const int bottom_y = 468 - timer_box_height;
  const int white_y = ui_single_player ? bottom_y - 5 : bottom_y;
  const int timer_y[] = {top_y, white_y};
  
  setTextColor(WHITE);
  setTextSize(2);
  char time_str[10];
  
  // draw both timer boxes
  for (int i = 0; i < 2; i++) {
    fillRect(timer_x, timer_y[i], timer_box_width, timer_box_height, BLACK);
    drawRect(timer_x, timer_y[i], timer_box_width, timer_box_height, WHITE);
    format_time(i ? white_time_remaining_us : black_time_remaining_us, time_str);
    setCursor(timer_x + timer_padding, timer_y[i] + timer_padding);
    writeString(time_str);
  }
  
  last_draw_white_sec = white_time_remaining_us / 1000000;
  last_draw_black_sec = black_time_remaining_us / 1000000;
}

// move history storage
static char moves[MOVE_HISTORY_MAX_MOVES * 2][6];
static int move_count = 0, display_start = 0;

// converts move to algebraic notation
static void move_to_notation(int from_r, int from_c, int to_r, int to_c, int piece, int captured_piece, char* buf) {
  char from_coord[3], to_coord[3];
  rc_to_coord(from_r, from_c, from_coord);
  rc_to_coord(to_r, to_c, to_coord);
  
  // handle castling notation
  if (piece == 5 && from_c == 4 && (to_c == 6 || to_c == 2)) {
    strcpy(buf, to_c == 6 ? "O-O" : "O-O-O");
    return;
  }
  
  // build standard notation
  static const char letters[] = {'\0', 'R', 'N', 'B', 'Q', 'K'};
  int idx = 0;
  if (piece >= 1 && piece <= 5) buf[idx++] = letters[piece];
  if (captured_piece != -1) {
    if (piece == 0) buf[idx++] = from_coord[0];
    buf[idx++] = 'x';
  }
  buf[idx++] = to_coord[0];
  buf[idx++] = to_coord[1];
  buf[idx] = '\0';
}

// clears move history
void init_move_history(void) {
  move_count = display_start = 0;
  memset(moves, 0, sizeof(moves));
}

// records move in history
void add_move(int from_r, int from_c, int to_r, int to_c, int piece, int color, int captured_piece) {
  if (move_count >= MOVE_HISTORY_MAX_MOVES) return;
  
  move_to_notation(from_r, from_c, to_r, to_c, piece, captured_piece, moves[move_count]);
  move_count++;
  
  // scroll if needed
  int total_lines = (move_count + 1) / 2;
  if (total_lines > MOVE_HISTORY_MAX_LINES)
    display_start = total_lines - MOVE_HISTORY_MAX_LINES;
  
  draw_move_history();
}

// draws move history list
void draw_move_history(void) {
  fillRect(MOVE_HISTORY_X, MOVE_HISTORY_Y, MOVE_HISTORY_WIDTH, 
           MOVE_HISTORY_MAX_LINES * MOVE_HISTORY_LINE_HEIGHT, BLACK);
  
  if (move_count == 0) return;
  
  setTextColor(WHITE);
  setTextSize(1);
  
  int y = MOVE_HISTORY_Y;
  int start_move = display_start * 2;
  
  // draw each move pair
  for (int i = start_move; i < move_count && (i - start_move) / 2 < MOVE_HISTORY_MAX_LINES; i += 2) {
    int move_num = (i / 2) + 1;
    char line[32];
    
    if (i + 1 < move_count)
      sprintf(line, "%2d. %-6s%-6s", move_num, moves[i], moves[i + 1]);
    else
      sprintf(line, "%2d. %-6s", move_num, moves[i]);
    
    setCursor(MOVE_HISTORY_X, y);
    writeString(line);
    y += MOVE_HISTORY_LINE_HEIGHT;
  }
}

// promotion menu state
static int promo_active = 0;
static int promo_piece_index = 0;
static int promo_selected_button = 0;

static const char* promo_piece_labels[] = { "Queen", "Rook", "Bishop", "Knight" };
static const int promo_piece_values[] = { QUEEN, ROOK, BISHOP, KNIGHT };

// draws pawn promotion menu
static void draw_promotion_menu(void) {
  if (!promo_active) return;
  
  const int w = MENU_BUTTON_WIDTH;
  const int h = TEXT_HEIGHT + MENU_PADDING * 2;
  const int menu_h = h * 2 + MENU_SPACING;
  const int x = MOVE_HISTORY_X;
  const int y0 = 434 - menu_h - 4;
  
  fillRect(x, y0, w, menu_h, BLACK);
  
  // draw piece selector button
  menu_button_t btn = {
    BUTTON_TOGGLE, x, y0, w, h,
    NULL, (char**)promo_piece_labels, 4,
    promo_piece_index, 1, NULL, NULL
  };
  draw_toggle_button(&btn, promo_selected_button == 0);
  
  // draw confirm button
  btn.type = BUTTON_ACTION;
  btn.y = y0 + h + MENU_SPACING;
  btn.label = "Promote";
  btn.options = NULL;
  draw_action_button(&btn, promo_selected_button == 1);
}

// hides promotion menu and restores display
static void hide_promotion_menu(void) {
  if (!promo_active) return;
  promo_active = 0;
  const int h = TEXT_HEIGHT + MENU_PADDING * 2;
  const int menu_h = h * 2 + MENU_SPACING;
  const int x = MOVE_HISTORY_X;
  const int y0 = 434 - menu_h - 4;
  fillRect(x, y0, MENU_BUTTON_WIDTH, menu_h, BLACK);
  draw_move_history();
}

// shows promotion menu for pawn
void show_promotion_menu(int color) {
  promo_active = 1;
  promo_piece_index = 0;
  promo_selected_button = 0;
  draw_promotion_menu();
}

// returns if promotion menu is showing
int promotion_menu_active(void) {
  return promo_active;
}

// handles promotion menu input
int process_promotion_input(int key_index) {
  if (!promo_active || key_index < 0 || key_index >= NUMKEYS) return 0;
  
  // select button pressed
  if (key_index == 1) {
    if (promo_selected_button == 0) {
      promo_piece_index = (promo_piece_index + 1) % 4;
      draw_promotion_menu();
    } else {
      hide_promotion_menu();
      return 1;
    }
  } 
  // up/down changes selection
  else if (key_index == 0 || key_index == 2) {
    promo_selected_button ^= 1;
    draw_promotion_menu();
  }
  return 0;
}

// returns selected promotion piece
int get_promotion_choice(void) {
  return promo_piece_values[promo_piece_index];
}

// game over menu state
static int game_over_menu_visible = 0;

// draws play again button
void ui_draw_game_over_menu(void) {
  game_over_menu_visible = 1;

  const int x = MOVE_HISTORY_X;
  const int y = MOVE_HISTORY_Y;
  const int w = MENU_BUTTON_WIDTH;
  const int h = TEXT_HEIGHT + MENU_PADDING * 2;

  fillRect(x, y, w, h, BLACK);
  fillRect(x, y, w, h, GREEN);
  drawRect(x, y, w, h, WHITE);

  setTextColor(BLACK);
  setTextSize(1);
  setCursor(x + MENU_PADDING, y + MENU_PADDING);
  writeString("Play Again");
}

// handles game over menu input
int ui_process_game_over_menu_input(int key_index) {
  if (!game_over_menu_visible) return 0;

  if (key_index == 1) {
    const int x = MOVE_HISTORY_X;
    const int y = MOVE_HISTORY_Y;
    const int w = MENU_BUTTON_WIDTH;
    const int h = TEXT_HEIGHT + MENU_PADDING * 2;
    fillRect(x, y, w, h, BLACK);

    game_over_menu_visible = 0;
    return 1;
  }

  return 0;
}

/////////////////// END AI-GENERATED CODE ///////////////////
