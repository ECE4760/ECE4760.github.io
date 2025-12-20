// ==========================================
// === Common Dependencies
// ==========================================
#include <math.h>
#include <pico.h>
#include <pico/mutex.h>
#include <pico/platform/common.h>
#include <pico/sem.h>
#include <pico/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Pico specifics
#include "pico/printf.h"
#include "pico/stdlib.h"
#include "pico/time.h"

// ==========================================
// === Math & Utils
// ==========================================
#include "smath/fix.h"
#include "smath/linalg.h"

// ==========================================
// === UART and Common I/O
// ==========================================
#include "hardware/adc.h"
#include "hardware/gpio.h"
// For overclock
#include "hardware/clocks.h"
#include "hardware/vreg.h"

// ==========================================
// === External Hardware Drivers (VGA, SPI)
// ==========================================
#include "driver/vga.h"
#include "hardware/dma.h"
#include "hardware/pio.h"

// DAC & Network
#include "hardware/spi.h"

// ==========================================
// === Protothreads
// ==========================================
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/multicore.h"
// protothreads header
#include "pt_cornell_rp2040_v1_4.h"

// ==========================================
// === Game Code
// ==========================================
#include "config/game.h"
#include "snake_state.h"

/* UTILITY MACROS */

#ifndef STR
#define STR(x) #x
#define XSTR(x) STR(x)
#endif

#define CLAMP(val, min, max) (MAX((min), (MIN((val), (max)))))

/* HARDWARE MACROS */

#define SYS_CLOCK_KHZ 270000

// Low-level alarm infrastructure we'll be using
#define ALARM_NUM 0
#define ALARM_IRQ TIMER_IRQ_0

// Pot: GPIO 27/ADC 1
#define GPIO_POT 27
#define ADC_NUM_POT 1

#define POT_MIN 0x000f
#define POT_MAX 0x0ff0
#define POT_RANGE (POT_MAX - POT_MIN)
#define POT_ZERO 2047

#define POT_ANGLE_MIN (-PI_O_FOUR15 >> 1)
#define POT_ANGLE_MAX (PI_O_FOUR15 >> 1)
#define POT_ANGLE_RANGE (POT_ANGLE_MAX - POT_ANGLE_MIN)

// Button: GPIO 26
#define GPIO_BUTTON 26

// User LED: GPIO 25
#define GPIO_LED 25

// Drawing indicator pin: GPIO 8
#define GPIO_DRAWING 8

/* GAME STATE */

// Signaling
semaphore_t calc_start, draw_start, dt_ready;
semaphore_t done_draw_snakes, done_draw_pellets;
semaphore_t done_calc_snakes, done_calc_pellets;

// Mutex
mutex_t selected_m, state_m, time_m, control_m;

// delta t conversion
#define MS_PER_US 33 // 1 ms / 1000 us

int32_t spare_time = 0;
uint32_t time_elapsed_s = 0;
uint32_t time_elapsed_ms = 0;
static fix15 dt_ms;
static uint32_t draw_time;

#ifdef START_TIMESCALE
fix15 time_scale = (fix15)(START_TIMESCALE * 32768.0f);
#else
fix15 time_scale = ONE15;
#endif

enum play_states { NONE = 0, INIT, ACTIVE, PAUSED };
enum play_states pstate = NONE;

fix15 man_snake_angle;

#if (START_CONTROLLING)
bool manual_control = true;
#else
bool manual_control = false;
#endif

snake_t *selected_snake = &snakes[0];
pellet_t *selected_pellet = &pellets[0];
hvec2d_t s_dispv;
hvec2d_t ss_dispv;

void toggle_pause() {
  if (pstate == PAUSED) {
    pstate = ACTIVE;
  } else if (pstate == ACTIVE) {
    pstate = PAUSED;
  }
}

snake_t *get_best_snake() {
  int max_len = 0;
  snake_t *best = &snakes[0];
  for (int i = 0; i < MAX_SNAKES; i++) {
    if (snakes[i].num_segments > max_len) {
      best = &snakes[i];
      max_len = best->num_segments;
    }
  }

  return best;
}

/* DRAWING */

// uS per frame
// #define FRAME_RATE 16666
#define FRAME_RATE 33333

#define VSYNC_LEN_US 6400
#define VBLANK_LEN_US 36000

#define TEXT_X 10
#define TEXT_Y (board_top)
#define DIAL_X (TEXT_X)
#define DIAL_Y (board_top + 155)
#define DIAL_W (board_left - DIAL_X - 6)

// Screen size parameters
#define SCREEN_HEIGHT 480
#define SCREEN_WIDTH 640
const int disp_bottom = SCREEN_HEIGHT - 1;
const int disp_top = 0;
const int disp_left = 0;
const int disp_right = SCREEN_WIDTH - 1;
const int middle_x = SCREEN_WIDTH / 2;
const int middle_y = SCREEN_HEIGHT / 2;

short text_bottom = 0;
short text_right = 0;

// Snake game-field
const int board_bottom = disp_bottom - 20;
const int board_top = disp_top + 40;
const int board_left = disp_left + 160;
const int board_right = disp_right - 10;

// shared variable for erase color
const color_t bkgnd_color = BLACK;
const color_t frame_color = DARK_GREEN;
const color_t text_color = WHITE;
const color_t border_color = WHITE;

// Param/stat strings
static const char num_snakes_format[] = "# Snakes: %d";
static const char num_pellets_format[] = "# Pellets: %d";
static const char time_format[] = "Time elapsed: %u";
static const char time_scale_format[] = "Speed: %d%%";

static const char snake_info_format[10][32] = {"%s snake info:",
                                               "\xc2\xc3"
                                               "SID   : %hhu",
                                               "\xc2\xc3"
                                               "Target: %hu",
                                               "\xc2\xc3"
                                               "X,Y   : %d , %d",
                                               "\xc2\xc3"
                                               "HX,HY : %.2f , %.2f",
                                               "\xc2\xc3"
                                               "VX,VY : %.2f , %.2f",
                                               "\xc2\xc3"
                                               "Speed : %.3f",
                                               "\xc2\xc3"
                                               "Angle : %d"
                                               "\xf7 "
                                               "\xaf\xaf"
                                               " %d\xf7",
                                               "\xc2\xc3"
                                               "Score : %u",
                                               "\xbf\xc3"
                                               "Length: %hu"};

static char screen_text_buff[128];

// Large screen change flags
bool restart_graphics = false;
bool full_redraw_f = false;
// Basic drawing changes
bool show_sid = SHOW_SID;
bool active_selection = false;

#if SHOW_HVEC
#if !defined(HVEC_COLOR)
#define HVEC_COLOR WHITE
#endif
#define draw_hvec(snake_p)                                                     \
  {                                                                            \
    ((void)drawLine(                                                           \
        fix152int(((snake_p)->head_pos).x),                                    \
        fix152int(((snake_p)->head_pos).y),                                    \
        fix152int((((snake_p)->head_pos).x) +                                  \
                  multfix15(((snake_p)->heading).x, int2fix15(HEAD_RADIUS))),  \
        fix152int((((snake_p)->head_pos).y) +                                  \
                  multfix15(((snake_p)->heading).y, int2fix15(HEAD_RADIUS))),  \
        HVEC_COLOR));                                                          \
  }
#else
#define draw_hvec(snake_p)
#endif

#if SHOW_VVEC
#if !defined(VVEC_COLOR)
#define VVEC_COLOR WHITE
#endif
#define draw_vvec(snake_p)                                                     \
  {                                                                            \
    ((void)drawLine(                                                           \
        fix152int((((snake_p)->head_pos).x)),                                  \
        fix152int((((snake_p)->head_pos).y)),                                  \
        fix152int(((((snake_p)->head_pos).x) +                                 \
                   multfix15(((snake_p)->heading).x, (snake_p)->speed))),      \
        fix152int(((((snake_p)->head_pos).y) +                                 \
                   multfix15(((snake_p)->heading).y, (snake_p)->speed))),      \
        VVEC_COLOR));                                                          \
  }
#else
#define draw_vvec(snake_p)
#endif

#define draw_sid(snake_p)                                                      \
  if (show_sid) {                                                              \
    ((void)drawChar((fix152int((snake_p)->head_pos.x) - HEAD_RADIUS / 2 + 1),  \
                    (fix152int((snake_p)->head_pos.y) - HEAD_RADIUS / 2),      \
                    ('0' + (char)((snake_p)->id)), SID_COLOR, BLACK, 1));      \
  }

// Draw a vector with origin at (x,y)
void draw_vec(fix15 x, fix15 y, hvec2d_t vec, color_t color) {
  drawLine(fix152int(x), fix152int(y), fix152int(vec.x + x),
           fix152int(vec.y + y), color);
}

void draw_vec_int(int x, int y, hvec2d_t vec, color_t color) {
  drawLine(x, y, fix152int(vec.x + int2fix15(x)),
           fix152int(vec.y + int2fix15(y)), color);
}

// Draw the snake debug info to the screen, with the top corner at (`x`,`y`)
// with each line `spacing` pixels apart (does not change text size or color).
// Returns new cursor y-position
// Note: Modifies VGA cursor
int draw_snake_info(snake_t *snake, short x, short y, short spacing) {
  hvec2d_t velocity = vec_scale(snake->heading, snake->speed);
  char str[] = "Selected";

  if (!active_selection)
    strcpy(str, "Best");

  sprintf(screen_text_buff, snake_info_format[0], str);
  setCursor(x, y);
  writeString(screen_text_buff);

  y += spacing;
  sprintf(screen_text_buff, snake_info_format[1], snake->id);
  setCursor(x, y);
  writeString(screen_text_buff);

  y += spacing;
  sprintf(screen_text_buff, snake_info_format[2], selected_pellet->id);
  setCursor(x, y);
  writeString(screen_text_buff);

  y += spacing;
  sprintf(screen_text_buff, snake_info_format[3], fix152int(snake->head_pos.x),
          fix152int(snake->head_pos.y));
  setCursor(x, y);
  writeString(screen_text_buff);

  y += spacing;
  sprintf(screen_text_buff, snake_info_format[4], fix152float(snake->heading.x),
          fix152float(snake->heading.y));
  setCursor(x, y);
  writeString(screen_text_buff);

  y += spacing;
  sprintf(screen_text_buff, snake_info_format[5], fix152float(velocity.x),
          fix152float(velocity.y));
  setCursor(x, y);
  writeString(screen_text_buff);

  y += spacing;
  sprintf(screen_text_buff, snake_info_format[6], fix152float(snake->speed));
  setCursor(x, y);
  writeString(screen_text_buff);

  y += spacing;
  sprintf(screen_text_buff, snake_info_format[7],
          fix152int(rad_to_deg15(snake_get_world_angle(snake))),
          fix152int(rad_to_deg15(snake_get_goal_world_angle(snake))));
  setCursor(x, y);
  writeString(screen_text_buff);

  y += spacing;
  sprintf(screen_text_buff, snake_info_format[8], snake->score);
  setCursor(x, y);
  writeString(screen_text_buff);

  y += spacing;
  sprintf(screen_text_buff, snake_info_format[9], snake->num_segments);
  setCursor(x, y);
  writeString(screen_text_buff);

  return y + spacing;
}

// Draw a square dial on the screen at the specified position.
// Clobbers all text related state (buffer, cursor, text size, and color)
void draw_angle_dial(int x_start, int y_start, int w) {
  clearRect(x_start, y_start, x_start + w, y_start + w, bkgnd_color);
  drawRect(x_start, y_start, w, w, WHITE);
  color_t pcolor = WHITE;

  fix15 angle_diff = abs(POT_ANGLE_RANGE - abs(man_snake_angle << 1));
  if (angle_diff <= PI_O_FOUR15 >> 2) {
    pcolor = RED;
  } else if (angle_diff <= PI_O_FOUR15 >> 1) {
    pcolor = ORANGE;
  }

  int x = x_start + (w / 2);
  int y = y_start + (w / 2);

  hvec2d_t man_angle_vec =
      vec_rotate(make_vec_int(0, y_start - y), man_snake_angle << 2);
  hvec2d_t snake_angle_vec =
      vec_scale(selected_snake->heading, int2fix15(w / 3));

  crosshair(x, y, WHITE);
  draw_vec_int(x, y, man_angle_vec, pcolor);
  draw_vec_int(x, y, snake_angle_vec, DARK_GREEN);

  setTextSize(1);

  setCursor(x_start + 3, y_start + 3);
  setTextColor(pcolor);
  sprintf(screen_text_buff, "%-.1f\xf7",
          fix152float(rad_to_deg15(man_snake_angle)));
  writeString(screen_text_buff);

  setCursor(x_start + w - 40, y_start + 3);
  setTextColor(MED_GREEN);
  sprintf(screen_text_buff, "%.1f\xf7",
          fix152float(rad_to_deg15(snake_get_world_angle(selected_snake))));
  writeString(screen_text_buff);
}

// Redraw text with box top-left corner at (x_start, y_start)
int redraw_text(int x_start, int y_start) {
  // Clear old text
  clearRect(x_start, y_start, x_start + 146, y_start + 148, bkgnd_color);
  drawRect(x_start, y_start, 144, 146, BORDER_COLOR);

  text_bottom = y_start + 148;
  text_right = x_start + 146;

  setTextColor(WHITE);

  // Basic info
  // setCursor(x_start, y_start);
  // writeString("Raspberry Pi Pico");
  // y += 10; // Keep track of cursor
  // setCursor(x_start, y);
  // writeString("Distributed Snake");

  int y = y_start + 3;
  int x = x_start + 3;

  setCursor(x, y);
  switch (pstate) {
  case INIT:
    setTextColor2(bkgnd_color, WHITE);
    sprintf(screen_text_buff, "INIT");
    break;
  case ACTIVE:
    sprintf(screen_text_buff, "ACTIVE");
    break;
  case PAUSED:
    sprintf(screen_text_buff, "PAUSED");
    break;
  case NONE:
  default:
    sprintf(screen_text_buff, "NONE");
    break;
  }
  writeString(screen_text_buff);

  // Stats
  y += 10;
  setCursor(x, y);
  sprintf(screen_text_buff, num_snakes_format, MAX_SNAKES);
  writeString(screen_text_buff);

  y += 10;
  setCursor(x, y);
  sprintf(screen_text_buff, num_pellets_format, MAX_PELLETS);
  writeString(screen_text_buff);

  y += 10;
  setCursor(x, y);
  sprintf(screen_text_buff, time_format, time_elapsed_s);
  writeString(screen_text_buff);

  y += 10;
  setCursor(x, y);
  sprintf(screen_text_buff, time_scale_format,
          fix152int(multfix15(time_scale, int2fix15(100))));
  writeString(screen_text_buff);

  y += 15;
  y += draw_snake_info(selected_snake, x, y, 8);

  return y;
}

void redraw_background() {
  // Clear screen
  clearLowFrame(0, frame_color);

  redraw_text(TEXT_X, TEXT_Y);
  drawRect(board_left - 1, board_top - 1, (board_right - board_left + 2),
           (board_bottom - board_top + 2), BORDER_COLOR);

  if (manual_control) {
    draw_angle_dial(DIAL_X, DIAL_Y, DIAL_W);
  }
}

void draw_segments(snake_t *snk) {
  segment_t *seg;

  color_t snake_color = MED_GREEN;
  color_t snake_color_alt = snk == selected_snake ? GREEN : DARK_GREEN;
  for (int i = 0; i < snk->num_segments; i++) {
    seg = &snk->body[i];

    if (seg->start_pos.x < int2fix15(board_left) ||
        seg->start_pos.x > int2fix15(board_right) ||
        seg->start_pos.y < int2fix15(board_top) ||
        seg->start_pos.y > int2fix15(board_bottom))
      break;

    draw_vec(seg->start_pos.x, seg->start_pos.y, seg->end_offset, snake_color);
    snake_color = snake_color == MED_GREEN ? snake_color_alt : MED_GREEN;

#if SHOW_SEG_JOINTS
    fillCircle(fix152int(seg->start_pos.x), fix152int(seg->start_pos.y), 1,
               WHITE);
#endif
  }

#if SHOW_SEG_JOINTS
  fillCircle(fix152int(seg->start_pos.x + seg->end_offset.x),
             fix152int(seg->start_pos.y + seg->end_offset.y), 1, WHITE);
#endif
}

// ==================================================
// === graphics demo -- RUNNING on core 0
// ==================================================
static PT_THREAD(protothread_graphics(struct pt *pt)) {
  PT_BEGIN(pt);

  // Setup GPIO to check draw-times on a scope
  gpio_init(GPIO_DRAWING);
  gpio_set_dir(GPIO_DRAWING, GPIO_OUT);
  gpio_put(GPIO_DRAWING, false);

  // Init the redraw flag
  full_redraw_f = false;

  // Timekeeping
  static uint8_t frame = 0;
  time_elapsed_s = 0;
  time_elapsed_ms = 0;

  // Variables for maintaining frame rate
  static absolute_time_t loop_start_time;
  static absolute_time_t loop_end_time;
  static uint32_t begin_time;
  draw_time = 0;

  static char frame_rate[16]; // 15 char string for displaying frame rate

  // Default for drawing is white on black background w/ smallest text size
  setTextColor2(WHITE, BLACK);
  setTextSize(1);

  redraw_background();

  PT_SEM_SDK_WAIT(pt, &draw_start);
  PT_INTERVAL_INIT();
  while (!gpio_get(VSYNC)) {
    tight_loop_contents();
  }
  while (true) {
    loop_start_time = get_absolute_time();
    // restart logic
    if (restart_graphics) {
      // reset the flag
      restart_graphics = false;
      // restarts the current thread at PT_BEGIN(pt);
      // as if it has not executed before
      PT_RESTART(pt);
    }

    // DOES NOT CHECK for an edge, so very fast drawing
    // MAY need a 1 mSec yield at the end
    PT_MUTEX_SDK_AQUIRE(pt, &control_m);
    PT_MUTEX_SDK_AQUIRE(pt, &selected_m);
    while (gpio_get(VSYNC)) {
    };
    gpio_put(GPIO_DRAWING, true);
    // clear takes 200 uSec
    // clearRect(disp_left, disp_top, disp_right, disp_bottom,
    // (short)bkgnd_color);
    begin_time = PT_GET_TIME_usec();

    if (full_redraw_f) {
      redraw_background();
      full_redraw_f = false;
    } else {
      redraw_text(TEXT_X, TEXT_Y);
      if (manual_control) {
        draw_angle_dial(DIAL_X, DIAL_Y, DIAL_W);
      }
    }

    if (!manual_control && !active_selection) {
      selected_snake = get_best_snake();
    }
    PT_MUTEX_SDK_RELEASE(&selected_m);
    PT_MUTEX_SDK_RELEASE(&control_m);

    // Draw framerate
    if ((frame & 0b111) == 0b111) {
      sprintf(frame_rate, "%-5.3fms", (((float)draw_time / 1000.0)));
      setTextColor2(WHITE, BLACK);
      setCursor(board_right - 65, board_top - 20);
      writeStringBig(frame_rate);
    }

    clearRect(board_left, board_top, board_right + 2, board_bottom + 2, BLACK);
    drawRect(board_left - 1, board_top - 1, (board_right - board_left + 2),
             (board_bottom - board_top + 2), BORDER_COLOR);

    // frame number
    frame++;
    // gpio_put(GPIO_DRAWING, false);
    // PT_YIELD_UNTIL(pt, !gpio_get(VSYNC));
    // gpio_put(GPIO_DRAWING, true);

    PT_SEM_SDK_WAIT(pt, &done_calc_pellets);
    for (int i = 0; i < MAX_PELLETS; i++) {
      color_t pellet_color =
          &pellets[i] == selected_pellet && !manual_control ? LIGHT_BLUE : RED;
      drawCircle(fix152int(pellets[i].position.x),
                 fix152int(pellets[i].position.y), PELLET_RADIUS, pellet_color);
    }
    PT_SEM_SDK_SIGNAL(pt, &done_draw_pellets);

    PT_SEM_SDK_WAIT(pt, &done_calc_snakes);
    for (int i = 0; i < MAX_SNAKES; i++) {
      snake_t *snk = &snakes[i];

      color_t snake_color = snk == selected_snake ? GREEN : DARK_GREEN;
      drawCircle(fix152int(snk->head_pos.x), fix152int(snk->head_pos.y),
                 HEAD_RADIUS, snake_color);
      draw_hvec(snk);
      draw_vvec(snk);
      draw_sid(snk);

      if (snk->num_segments > 0) {
        draw_segments(snk);
      }

      if (snk == selected_snake && !manual_control) {
        draw_vec(snk->head_pos.x, snk->head_pos.y, s_dispv, LIGHT_BLUE);
        draw_vec(snk->head_pos.x, snk->head_pos.y, ss_dispv, RED);
      }

      if (snk == selected_snake && manual_control) {
        hvec2d_t scaled_ind = vec_scale(snk->goal_heading, ONE15 << 4);
        draw_vec(snk->head_pos.x, snk->head_pos.y, scaled_ind, WHITE);
      }
    }

    PT_SEM_SDK_SIGNAL(pt, &done_draw_snakes);

    draw_time = PT_GET_TIME_usec() - begin_time;
    spare_time = FRAME_RATE - draw_time;
    dt_ms = multfix15(int2fix15(CLAMP(FRAME_RATE, 1, INT2FIX15_MAX)), MS_PER_US);
    PT_SEM_SDK_SIGNAL(pt, &dt_ready);
    loop_end_time = get_absolute_time();

    time_elapsed_ms +=
        to_ms_since_boot(loop_end_time) - to_ms_since_boot(loop_start_time);

    while (time_elapsed_ms >= 1000) {
      time_elapsed_s++;
      time_elapsed_ms -= 1000;
    }

    spare_time = FRAME_RATE - draw_time;
    gpio_put(GPIO_DRAWING, false);
    //  delay in accordance with frame rate

    // Thread is paced by the Vsync edge
    // PT_YIELD(pt);
    // PT_YIELD_UNTIL_usec(!gpio_get(VSYNC), spare_time);
    if (spare_time > 0) {
      PT_YIELD_INTERVAL(spare_time - 100);
    }
  }
  PT_END(pt);
} // graphics thread

/* MAIN GAME LOOP */

static PT_THREAD(protothread_gameloop(struct pt *pt)) {
  PT_BEGIN(pt);
  PT_MUTEX_SDK_AQUIRE(pt, &state_m);
  pstate = INIT;

  static absolute_time_t loop_start;
  static fix15 dt_ms_loc = 1;
  loop_start = get_absolute_time();
  snake_game_init(board_left, board_top, board_right, board_bottom);

  snake_game_start(MAX_SNAKES, MAX_PELLETS);
  pstate = ACTIVE;
  PT_MUTEX_SDK_RELEASE(&state_m);

  PT_SEM_SDK_SIGNAL(pt, &draw_start);
  PT_SEM_SDK_SIGNAL(pt, &done_calc_pellets);
  PT_SEM_SDK_SIGNAL(pt, &done_calc_snakes);
  PT_SEM_SDK_WAIT(pt, &done_draw_snakes);
  while (1) {
    // PT_MUTEX_SDK_AQUIRE(pt, &state_m);
    if (pstate == ACTIVE) {
      // PT_MUTEX_SDK_RELEASE(&state_m);
      loop_start = get_absolute_time();

      for (int i = 0; i < MAX_SNAKES; i++) {
        snake_check_pellets(&snakes[i]);
      }

      PT_SEM_SDK_SIGNAL(pt, &done_calc_pellets);
      PT_SEM_SDK_WAIT(pt, &dt_ready);
      dt_ms_loc = dt_ms;

      for (int i = 0; i < MAX_SNAKES; i++) {
        snake_t *snk = &snakes[i];

        snake_update_position(snk, multfix15(dt_ms_loc, time_scale));
        snake_check_collision(snk);

        if (snk == selected_snake && manual_control) {
          // Offset current heading by parsed input angle
          // fix15 new_heading = man_snake_angle + PI_O_TWO15;
          // snake_change_angle(snk, new_heading);
          snake_turn(snk, man_snake_angle);
        } else {
          if (snk == selected_snake) { //If watching this snake, find the closest segment of another snake
            fix15 sclosest_dist;
            snake_find_closest_snake(snk, &ss_dispv, &sclosest_dist);
          }
          
          snake_ai_choose_goal_fix15(snk, &selected_pellet, &s_dispv,
                                     snk == selected_snake);
          // snake_change_heading(&snakes[i], PI);
        }
      }
      PT_SEM_SDK_SIGNAL(pt, &done_calc_snakes);
    } else {
      PT_MUTEX_SDK_RELEASE(&state_m);
      PT_SEM_SDK_SIGNAL(pt, &done_calc_pellets);
      PT_SEM_SDK_SIGNAL(pt, &done_calc_snakes);
    }

    PT_YIELD(pt);
  }
  PT_END(pt);
}

static PT_THREAD(protothread_clear(struct pt *pt)) {
  PT_BEGIN(pt);

  static short top_y = 0;
  static short bottom_y = board_bottom;
  static short left_x = 0;
  static short right_x = board_right;

  PT_YIELD_UNTIL(pt, text_right > 0);
  PT_YIELD_UNTIL(pt, text_bottom > 0);
  while (1) {

    PT_YIELD_UNTIL(pt, !gpio_get(VSYNC));
    drawHLine(0, top_y, board_right - 66, frame_color);
    drawHLine(0, bottom_y, SCREEN_WIDTH - 1, frame_color);
    drawVLine(right_x, 0, SCREEN_HEIGHT - 1, frame_color);

    if (left_x < TEXT_X || left_x > text_right) {
      drawVLine(left_x, 0, SCREEN_HEIGHT - 1, frame_color);
    } else {
      drawVLine(left_x, 0, TEXT_X - 1, frame_color);
      drawVLine(left_x, text_bottom, SCREEN_HEIGHT - text_bottom, frame_color);
    }

    top_y++;
    bottom_y++;
    left_x++;
    right_x++;

    if (top_y >= board_top - 1)
      top_y = 0;

    if (bottom_y >= SCREEN_HEIGHT)
      bottom_y = board_bottom + 1;

    if (left_x >= board_left)
      left_x = 0;

    if (right_x >= SCREEN_WIDTH)
      right_x = board_right + 1;

    PT_YIELD(pt);
  }

  PT_END(pt);
}

/* EXTERNAL I/O */

// Debounce state
#define NOT_PRESSED 0
#define MAYBE_PRESSED 1
#define MAYBE_NOT_PRESSED 2
#define PRESSED 3

unsigned int debounce_state = NOT_PRESSED; // Start unpressed
bool possible = 0x00;

// Trigger action on button press
void inline button_pressed() { toggle_pause(); }

static PT_THREAD(protothread_controls(struct pt *pt)) {
  PT_BEGIN(pt);

  // Setup ADC hardware for potentiometer
  adc_init();
  // Setup pot
  adc_gpio_init(GPIO_POT);
  adc_fifo_setup(true, false, 0, false, false);
  adc_select_input(ADC_NUM_POT);

  // Setup button
  gpio_init(GPIO_BUTTON);
  gpio_set_dir(GPIO_BUTTON, GPIO_IN);
  gpio_pull_down(GPIO_BUTTON);

  // Debouncing
  debounce_state = NOT_PRESSED;
  possible = false;
  static bool i;

  // Potentiometer
  static uint32_t pot_accum = 0;
  static uint8_t fifo_samples;
  static uint16_t pot_val = 0;
  static uint16_t old_pot_val = 0;
  static fix15 snake_angle = 0;

  // Start ADC sampling to FIFO
  adc_run(true);

  PT_INTERVAL_INIT();
  while (1) {

    // Check the button
    i = gpio_get(GPIO_BUTTON);

    switch (debounce_state) {
    case PRESSED:
      if (i != possible) {
        debounce_state = MAYBE_NOT_PRESSED;
      }
      break;
    case MAYBE_PRESSED: // Update on MAYBE->PRESSED
      if (i == possible) {
        button_pressed();
        debounce_state = PRESSED;
      } else {
        debounce_state = NOT_PRESSED;
      }
      break;
    case MAYBE_NOT_PRESSED:
      if (i == possible) {
        debounce_state = PRESSED;
      } else {
        debounce_state = NOT_PRESSED;
      }
      break;
    case NOT_PRESSED:
    default:
      if (i) {
        possible = i;
        debounce_state = MAYBE_PRESSED;
      } else {
        debounce_state = NOT_PRESSED;
      }
      break;
    }

    // Check the POT
    pot_accum = 0;
    fifo_samples = 0;

    // Clear the FIFO
    while (!adc_fifo_is_empty()) {
      pot_accum += adc_fifo_get();
      fifo_samples++;
    }

    // Get the average of the last samples
    pot_val = pot_accum / fifo_samples;

    pot_val = CLAMP(pot_val, POT_MIN, POT_MAX);
    snake_angle = int2fix15(((int32_t)pot_val - POT_MIN));
    snake_angle = multfix15(POT_ANGLE_RANGE, snake_angle);
    snake_angle = divfix15(snake_angle, int2fix15(POT_RANGE)) + POT_ANGLE_MIN;

    man_snake_angle = snake_angle;

    PT_YIELD_INTERVAL(10000);
  }

  PT_END(pt);
}

// ==================================================
// === user's serial input thread on core 0
// ==================================================
// serial_read an serial_write do not block any thread
// except this one
static PT_THREAD(protothread_serial(struct pt *pt)) {
  PT_BEGIN(pt);
  static char flag;
  static float new_time;
  static int num;
  static bool list_stop;

  static pellet_t *pel;
  static snake_t *snk;

  // clang-format off
  // NOLINTBEGIN
  static const char help_text[] = 
      "Commands:\n"
      "\th         : Show this text\n"
      "\tq         : Pause the game\n"
      "\ti         : Show snake IDs (SID)\n"
      "\tt <float> : Set the time scale to <float> (<float> > 0)\n"
      "\tc <int>   : Control snake with ID <int> until you press another key"
        "(0 < <int> < " XSTR(MAX_SNAKES) ")\n"
      "\ts <int>   : Select snake with ID <int> and print info until you press"
        " another key (0 < <int> < " XSTR(MAX_SNAKES) ")\n"
      "\tp <int>   : Select pellet with ID <int> and print info until you press"
        " another key (0 < <int> < " XSTR(NUM_PELLETS) ")\n\n\0";
  // NOLINTEND
  // clang-format on

  static int written = 0;
  static int remaining = sizeof(help_text);
  static int transfer_size;

  sprintf(pt_serial_out_buffer,
          "\n\nEnter `h[elp]` to get a list of commands!\n");
  serial_write;

  while (1) {
    num = 0;
    pel = &pellets[0];
    snk = &snk[0];

    // print prompt
    sprintf(pt_serial_out_buffer, " >> ");
    serial_write;

    serial_read;
    flag = pt_serial_in_buffer[0];

    switch (flag) {
    case ' ':
    case '\0':
    case '\r':
    case '\n':
      break;
    case 'i':
      show_sid = !show_sid;
      if (show_sid)
        sprintf(pt_serial_out_buffer, "Enabled SIDs\n");
      else
        sprintf(pt_serial_out_buffer, "Disabled SIDs\n");
      serial_write;
      break;
    case 't':
      sscanf(pt_serial_in_buffer + 1, "%f", &new_time);
      if (new_time > 0) {
        time_scale = float2fix15(new_time);
        sprintf(pt_serial_out_buffer, "Set timescale to: %f\n",
                fix152float(time_scale));
      } else {
        sprintf(pt_serial_out_buffer, "Scale must be greater than 0.");
      }
      serial_write;
      break;
    case 'q':
      toggle_pause();
      if (pstate == ACTIVE) {
        sprintf(pt_serial_out_buffer, "Game Resumed!\n");
      } else if (pstate == PAUSED) {
        sprintf(pt_serial_out_buffer, "Game Paused!\n");
      } else {
        sprintf(pt_serial_out_buffer, "Cannot pause right now!\n");
      }
      serial_write;
      break;
    case 'p':
      sscanf(pt_serial_in_buffer + 1, "%d", &num);

      if (num < MAX_PELLETS) {
        pel = &pellets[num];

        // Print debug information constantly until user types anything
        // Note: Must press enter after stopping the output

        // Clear the PT input buffer
        sscanf(pt_serial_in_buffer + 1, "\n");

        // Clear the UART FIFO to prevent spurious exit
        while (uart_is_readable(UART_ID)) {
          uart_getc(UART_ID);
        }

        // Print debug info until the user interrupts
        list_stop = false;
        while (!list_stop) {
          sprintf(pt_serial_out_buffer, "Pellet[%d]: (%f, %f, %f) - %d pts\n",
                  num, fix152float(pel->position.x),
                  fix152float(pel->position.y), fix152float(pel->position.w),
                  pel->value);
          serial_write;

          list_stop = false;
          PT_YIELD(pt);

          // Check for user input
          if (uart_is_readable(UART_ID)) {
            // serial_read;
            list_stop = true;
          }
          PT_YIELD_usec(1000000);
        }
      }
      break;

    case 'c':
      sprintf(pt_serial_out_buffer, "Use potentiometer to control "
                                    "highlighted snake (enter to stop) \n");
      manual_control = true;
      // Fall into 's' case to select the snake
    case 's':
      sscanf(pt_serial_in_buffer + 1, "%d", &num);

      if (num < MAX_SNAKES) {
        if (num < 1) {
          num = 1;
        }
        snk = &snakes[num - 1];
        selected_snake = snk;
        active_selection = true;

        // Print debug information constantly until user types anything
        // Note: Must press enter after stopping the output

        // Clear the PT input buffer
        sscanf(pt_serial_in_buffer + 1, "\r");

        // Clear the UART FIFO to prevent spurious exit
        while (uart_is_readable(UART_ID)) {
          uart_getc(UART_ID);
        }

        // Print debug info until the user interrupts
        list_stop = false;
        while (!list_stop) {
          sprintf(pt_serial_out_buffer,
                  "Snake[%d]: (%f, %f, %f) [%d deg -> %d deg] - %hu long\n",
                  snk->id, fix152float(snk->head_pos.x),
                  fix152float(snk->head_pos.y), fix152float(snk->head_pos.w),
                  fix152int(rad_to_deg15(snake_get_world_angle(snk))),
                  fix152int(rad_to_deg15(snake_get_goal_world_angle(snk))),
                  snk->num_segments);
          serial_write;

          list_stop = false;
          PT_YIELD(pt);

          // Check for user input
          if (uart_is_readable(UART_ID)) {
            // serial_read;
            list_stop = true;
          }
          PT_YIELD_usec(1000000);
        }

        if (manual_control)
          full_redraw_f = true;
        manual_control = false;
        active_selection = false;
      }
      break;
    case 'h':
    default:
      // Clear extraneous input
      sscanf(pt_serial_in_buffer + 1, "\r");
      written = 0;
      remaining = sizeof(help_text);
      transfer_size = 0;
      do {
        transfer_size = MIN(sizeof(pt_serial_out_buffer) - 1, remaining);
        memcpy(pt_serial_out_buffer, &help_text[written], transfer_size);
        pt_serial_out_buffer[written + transfer_size] = '\0';
        written += transfer_size;
        remaining -= transfer_size;
        serial_write;
        PT_YIELD(pt);
      } while (remaining > 0 && written < sizeof(help_text));
    }
    PT_YIELD_usec(10000);
  } // END WHILE(1)
  PT_END(pt);
} // serial thread

/* HEARTBEAT */

static PT_THREAD(protothread_blinky(struct pt *pt)) {
  PT_BEGIN(pt);

  int interval = get_core_num() == 0 ? 500000 : 1200000;
  static bool led_state = false;

  gpio_init(GPIO_LED);
  gpio_set_dir(GPIO_LED, GPIO_OUT);

  gpio_put(GPIO_LED, led_state);

  PT_INTERVAL_INIT();
  while (1) {
    // Every half-second
    PT_YIELD_INTERVAL(interval);

    gpio_put(GPIO_LED, led_state);
    led_state = !led_state;
  }
  PT_END(pt);
}

/* SERVER MESSAGING */

static PT_THREAD(protothread_messaging(struct pt *pt)) {
  PT_BEGIN(pt);
  //TODO: Finish server messaging
  PT_END(pt);
}

/* INITS/MAIN */

// ========================================
// === core 1 main -- started in main below
// ========================================
void core1_main() {

  printf("CORE 1\n\r");
  printf("RUNNING: blinky, serial, and controls\n\r");

  //
  //  === add threads  ====================
  // for core 1
  pt_add_thread(protothread_gameloop);
  pt_add_thread(protothread_controls);
  pt_add_thread(protothread_serial);
  pt_add_thread(protothread_blinky);
  pt_add_thread(protothread_clear);
  //
  // === initalize the scheduler ==========
  pt_schedule_start;
  // NEVER exits
  // ======================================
}

// ========================================
// === core 0 main
// ========================================
int main() {
  // set the clock
  vreg_set_voltage(VREG_VOLTAGE_MAX);
  set_sys_clock_khz(SYS_CLOCK_KHZ, true);

  // SERIAL //

  // VGA //

  // Initialize the VGA screen
  initVGA();

  // OTHER IO //
  // TODO: Add communication code

  // Initidalize stdio
  stdio_init_all();

  // Mutex & thread signaling
  sem_init(&dt_ready, 0, 1);
  sem_init(&draw_start, 0, 1);
  sem_init(&calc_start, 0, 1);
  sem_init(&done_calc_pellets, 0, 1);
  sem_init(&done_calc_snakes, 0, 1);
  sem_init(&done_draw_pellets, 0, 1);
  sem_init(&done_draw_snakes, 0, 1);

  mutex_init(&selected_m);
  mutex_init(&state_m);
  mutex_init(&time_m);
  mutex_init(&control_m);

  // // Initialize SPI channel (channel, baud rate set to 20MHz)
  // spi_init(SV_SPI_PORT, SPI_BAUD);

  // // Format SPI channel (channel, data bits per transfer, polarity, phase,
  // // order)
  // spi_set_format(SV_SPI_PORT, SPI_BITS, SPI_POL, SPI_PHASE, SPI_ORDER);

  // // Set to master
  // spi_set_slave(SV_SPI_PORT, false);

  // // Map SPI signals to GPIO ports, acts like framed SPI with this CS mapping
  // gpio_set_function(SV_SPI_MISO, GPIO_FUNC_SPI);
  // gpio_set_function(SV_SPI_CS0, GPIO_FUNC_SPI);
  // gpio_set_function(SV_SPI_SCK, GPIO_FUNC_SPI);
  // gpio_set_function(SV_SPI_MOSI, GPIO_FUNC_SPI);

  // announce the threader version on system reset
  printf("\n\rProtothreads RP2040/2350 v1.4 \n\r");
  printf("CORE 0\n\r");
  printf("RUNNING: graphics\n\r");

  // SNAKE //
  // start core 1 threads
  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // === config threads ========================
  // for core 0
  pt_add_thread(protothread_graphics);
  pt_add_thread(protothread_blinky);
  //
  // === initalize the scheduler ===============
  pt_schedule_start;
  // NEVER exits
  // ===========================================
} // end main
