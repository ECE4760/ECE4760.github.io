/**
 * Rachel Lee
 * Zephan Sanghani
 * Kailey Ye
 *
 * PicoChess: ECE4760 Final Project
 */

////////////////// BEGIN AI-GENERATED CODE //////////////////

#ifndef USER_INTERFACE_H
#define USER_INTERFACE_H

#include <stdint.h>

#define KEYPAD_ROW1_PIN 26
#define KEYPAD_COL_PIN  22
#define KEYROWS         3
#define NUMKEYS         3

typedef enum {
    MENU_MODE_SELECT,
    MENU_DONE
} menu_state_t;

typedef enum {
    BUTTON_TOGGLE,
    BUTTON_ACTION
} button_type_t;

typedef struct {
    button_type_t type;
    int x, y;
    int width, height;
    char* label;
    char** options;
    int num_options;
    int current_option;
    int is_visible;
    void (*toggle_cb)(void);
    int (*action_cb)(void);
} menu_button_t;

void init_keypad(void);
int scan_keypad(void);

void init_menu(void);
void draw_menu(void);
int process_menu_input(int key_index);
int is_menu_done(void);
int get_menu_game_mode(void);
int get_menu_player_color(void);
int get_menu_timer_minutes(void);
int is_debug_mode(void);
void menu_set_start_enabled(int enabled);

void init_timers(int minutes_per_player);
void update_timers(int current_turn);
void update_timers_continuous(void);
void draw_timers(void);
int check_timer_expired(void);
void ui_set_single_player(int is_single_player);

void init_move_history(void);
void add_move(int from_r, int from_c, int to_r, int to_c, int piece, int color, int captured_piece);
void draw_move_history(void);

void show_promotion_menu(int color);
int promotion_menu_active(void);
int process_promotion_input(int key_index);
int get_promotion_choice(void);

void ui_draw_game_over_menu(void);
int ui_process_game_over_menu_input(int key_index);

#endif

/////////////////// END AI-GENERATED CODE ///////////////////
