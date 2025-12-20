/**
 * Rachel Lee
 * Zephan Sanghani
 * Kailey Ye
 *
 * PicoChess: ECE4760 Final Project
 */

// gpio 0-7 -> 330 ohm -> chessboard rows
// gpio 8-15 -> 330 ohm -> chessboard cols
// gpio 16 -> vga hsync
// gpio 17 -> vga vsync
// gpio 18 -> 470 ohm -> vga green
// gpio 19 -> 330 ohm -> vga green
// gpio 20 -> 330 ohm -> vga blue
// gpio 21 -> 330 ohm -> vga red
// gpio 22 -> keypad col
// gpio 26-28 -> keypad rows

#include "vga16_graphics_v2.h"
#include "chess_rom.h"
#include "chess.h"
#include "computer.h"
#include "user_interface.h"
#include "chessboard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "pt_cornell_rp2040_v1_4.h"

static int game_mode = 0;

// handles keypad input and menu navigation
static PT_THREAD (protothread_keypad(struct pt *pt)) {
  PT_BEGIN(pt);
  
  init_menu();
  static int prev_key = -1;
  
  while (1) {
    int key = scan_keypad();
    
    // handle pawn promotion menu
    if (promotion_menu_active()) {
      if (key >= 0 && key != prev_key) {
        if (process_promotion_input(key))
          chess_apply_promotion(get_promotion_choice());
      }
      prev_key = key;
      PT_YIELD_usec(20000);
      continue;
    }
    
    // handle main menu
    if (!is_menu_done()) {
      if (key >= 0 && key != prev_key) {
        if (process_menu_input(key)) {
          // start game with selected settings
          game_mode = get_menu_game_mode();
          if (game_mode == 1) {
            int color = get_menu_player_color();
            init_computer(color ? 0 : 1);
          }
          ui_set_single_player(game_mode == 1);
          init_timers(get_menu_timer_minutes());
          init_move_history();
          draw_menu();
          chessboard_start_game(game_mode);
        }
      }
      prev_key = key;
      PT_YIELD_usec(20000);
      continue;
    }
    
    // handle game over menu
    if (chess_is_game_over()) {
      if (key >= 0 && key != prev_key) {
        if (ui_process_game_over_menu_input(key)) {
          // reset game for replay
          draw_initial_board();
          init_move_history();
          draw_move_history();
          init_menu();
          chessboard_reset_view();
          draw_menu();
        }
      }
      prev_key = key;
      PT_YIELD_usec(20000);
      continue;
    }
    
    // update timers during gameplay
    if (chessboard_get_state() != BOARD_VERIFY) {
      update_timers_continuous();
    }
    int loser = check_timer_expired();
    if (loser >= 0)
      chess_on_time_expired(loser);
    
    prev_key = key;
    PT_YIELD_usec(20000);
  }
  
  PT_END(pt);
}

// scans physical board and handles move detection
static PT_THREAD (protothread_board(struct pt *pt)) {
  PT_BEGIN(pt);
  
  static char from[3], to[3];
  static int last_setup_ok = -1;
  static int was_debug = 0;
  
  while (1) {
    chessboard_scan();
    
    // menu phase - check setup or show debug
    if (!is_menu_done()) {
      int in_debug = is_debug_mode();
      if (in_debug != was_debug) {
        chessboard_reset_view();
        was_debug = in_debug;
      }
      if (in_debug) {
        chessboard_debug_view();
      } else {
        // check if board is set up correctly
        int setup_ok = chessboard_check_setup();
        if (setup_ok != last_setup_ok) {
          menu_set_start_enabled(setup_ok);
          draw_menu();
          last_setup_ok = setup_ok;
        }
      }
    } 
    // game phase - process moves
    else if (!chess_is_game_over()) {
      if (chessboard_update()) {
        // computer's turn - calculate and execute move
        if (computer_make_move()) {
          computer_get_last_move(from, to);
          chessboard_computer_moved(from, to);
        }
      }
    }
    
    PT_YIELD_usec(30000);
  }
  
  PT_END(pt);
}

// main entry point
int main() {
  set_sys_clock_khz(150000, true);
  stdio_init_all();
  
  // initialize peripherals
  initVGA();
  chessboard_init();
  init_keypad();
  
  // draw initial board state
  draw_initial_board();
  
  // start protothreads
  pt_add_thread(protothread_keypad);
  pt_add_thread(protothread_board);
  
  pt_schedule_start;
}
