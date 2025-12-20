/**
 * ECE 4760: Lab 2
 * Digital Galton Board
 *
 * Chris Parker (dcp243@cornell.edu) and Wren Kahn (jk2878@cornell.edu)
 * Modifying demo code written by:
 *  Hunter Adams (vha3@cornell.edu)
 *
 * HARDWARE CONNECTIONS
 *
 * VGA:
 * - GPIO 16 ---> VGA Hsync
 * - GPIO 17 ---> VGA Vsync
 * - GPIO 18 ---> VGA Green lo-bit --> 470 ohm resistor --> VGA_Green
 * - GPIO 19 ---> VGA Green hi_bit --> 330 ohm resistor --> VGA_Green
 * - GPIO 20 ---> 330 ohm resistor ---> VGA-Blue
 * - GPIO 21 ---> 330 ohm resistor ---> VGA-Red
 * - RP2040 GND ---> VGA-GND
 *
 * DAC:
 * - GPIO 5 (pin 7) Chip select
 * - GPIO 6 (pin 9) SCK/spi0_sclk
 * - GPIO 7 (pin 10) MOSI/spi0_tx
 * - GPIO 2 (pin 4) GPIO output for timing ISR
 * - 3.3v (pin 36) -> VCC on DAC
 * - GND (pin 3)  -> GND on DAC
 *
 * Button:
 * - GPIO 26 (pin 31)
 *
 * POT:
 * - GPIO 27 (pin 32)
 *
 * RESOURCES USED
 *  - PIO state machines 0, 1, and 2 on PIO instance 0
 *  - DMA channels 0, 1, 2, 3, 4, and 5
 *  - 153.6 kBytes of RAM (for pixel color data)
 *  - XXX X of RAM (for sound effect storage)
 *
 * Protothreads v1.4
 * Threads:
 * core 0:
 * Graphics & physics
 * blink LED25
 * core 1:
 * SFX
 */
// ==========================================
// === VGA graphics library
// ==========================================
#include "driver/vga.h"
#include "hardware/dma.h"
#include "hardware/pio.h"

// ==========================================
// === Datatype and Serial Libraries
// ==========================================
#include "pico/float.h"
#include "pico/printf.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "pico/time.h"
//
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
//
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/vreg.h"

// ==========================================
// === protothreads globals
// ==========================================
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/multicore.h"
#include "string.h"
// protothreads header
#include "pt_cornell_rp2040_v1_4.h"

// ==========================================
// === DAC
// ==========================================
#include "hardware/spi.h"

// Low-level alarm infrastructure we'll be using
#define ALARM_NUM 0
#define ALARM_IRQ TIMER_IRQ_0

// Pot: GPIO 27/ADC 1
#define GPIO_POT 27
#define ADC_NUM_POT 1

// Button: GPIO 26
#define GPIO_BUTTON 26

// Debounce state
#define NOT_PRESSED 0
#define MAYBE_PRESSED 1
#define MAYBE_NOT_PRESSED 2
#define PRESSED 3

unsigned int debounce_state = NOT_PRESSED; // Start unpressed
bool possible = 0x00;

#define NO_PARAM 0
#define NUM_BALLS_PARAM 1
#define GRAVITY_PARAM 2
#define BOUNCE_PARAM 3
#define PARAM_MOD 4

unsigned int current_param = NO_PARAM;

// Macros for fixed-point arithmetic (faster than floating point)
typedef signed int fix15;
#define multfix15(a, b)                                                        \
  ((fix15)((((signed long long)(a)) * ((signed long long)(b))) >> 15))
#define float2fix15(a) ((fix15)((a) * 32768.0))
#define fix152float(a) ((float)(a) / 32768.0)
#define absfix15(a) abs(a)
#define int2fix15(a) ((fix15)((a) << 15))
#define fix152int(a) ((int)((a) >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix15(a, b) (fix15)((((signed long long)(a)) << 15) / (b))

// Macros for fixed-point arithmetic (faster than floating point)
typedef signed int fix8;
#define multfix8(a, b)                                                         \
  ((fix8)((((signed long long)(a)) * ((signed long long)(b))) >> 8))
#define float2fix8(a) ((fix8)((a) * 256.0))
#define fix82float(a) ((float)(a) / 256.0)
#define absfix8(a) abs(a)
#define int2fix8(a) ((fix8)((a) << 8))
#define fix82int(a) ((int)((a) >> 8))
#define char2fix8(a) (fix8)(((fix8)(a)) << 8)
#define divfix8(a, b) (fix8)((((signed long long)(a)) << 8) / (b))

/////////////////////////////// GRAPHICS ///////////////////////////////////////

// uS per frame
#define FRAME_RATE 33333
#define RATE_CONVERSION 0.000001f

#define PEG_RADIUS 6
#define PEG_V_SEP 19
#define PEG_H_SEP 38

bool text_update_f = false;

int32_t spare_time = 0;

// restart graphics flag
bool restart_graphics = false;
// shared variable for erase color
int bkgnd_color = 0;

// Params bounding box
#define PARAM_TEXT_LEFT 0
#define PARAM_TEXT_RIGHT 150
#define PARAM_TEXT_TOP 0
#define PARAM_TEXT_BOTTOM 100

// Param/stat strings
static const char num_format[] = "# Balls: %d";
static const char grav_format[] = "Gravity: %2.2f";
static const char bounce_format[] = "Bounciness: %2.2f";
static const char time_format[] = "Time elapsed: %llu";
static const char total_format[] = "Total dropped: %d";

char num_text[sizeof(num_format) + 16];
char grav_text[sizeof(grav_format) + 16];
char bounce_text[sizeof(bounce_format) + 16];
char time_text[sizeof(time_format) + 16];
char total_text[sizeof(total_format) + 16];

// Screen size parameters
const int bottom_side = 479;
const int top_side = 0;
const int SCREEN_HEIGHT = bottom_side - top_side;
const int left_side = 0;
const int right_side = 639;
const int SCREEN_WIDTH = right_side - left_side;
const int middle_x = SCREEN_WIDTH / 2;

///////////////////////////////// SOUND ////////////////////////////////////////

// Number of samples per period in sine table
#define sine_table_size 256
#define dac_table_size sine_table_size * 2

bool muted = false;

// Sine table
int raw_sin[sine_table_size];

// Table of values to be sent to DAC
unsigned short DAC_data[dac_table_size];

// Pointer to the address of the DAC data table
unsigned short *dac_address_pointer = &DAC_data[0];

// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

// SPI configurations
#define PIN_MISO 4
#define PIN_CS 5
#define PIN_SCK 6
#define PIN_MOSI 7
#define SPI_PORT spi0

// Number of DMA transfers per event
const uint32_t transfer_count = sine_table_size;

dma_channel_config c;
dma_channel_config c2;
int data_chan;
int ctrl_chan;

void play_waveform_once() {
  // Reset read address (via ctrl_chan DMA)
  if (!muted)
    dma_start_channel_mask(1u << ctrl_chan);
}

///////////////////////////////// PHYSICS //////////////////////////////////////

// Defaults
#ifndef MAX_BALLS
#define MAX_BALLS 4500
#endif

#define DEFAULT_BALL_RADIUS 2
#define DEFAULT_NUM_BALLS 4500
#define DEFAULT_GRAVITY float2fix15(0.75f)
#define DEFAULT_BOUNCINESS float2fix15(0.5f)
#define DEFAULT_VMAX int2fix15(5);

// Physics parameters
fix15 gravity = DEFAULT_GRAVITY;
fix15 bounciness = DEFAULT_BOUNCINESS;

fix15 vmax = DEFAULT_VMAX;

// Stats
int balls_dropped = 0;

// Ball
fix15 ball_radius = int2fix15(DEFAULT_BALL_RADIUS);
int ball_radius_int = DEFAULT_BALL_RADIUS;

struct ball {
  fix15 x;
  fix15 y;
  fix15 vx;
  fix15 vy;
  int last_peg;
} typedef ball_t;

// Balls
int num_balls = DEFAULT_NUM_BALLS;
ball_t balls[MAX_BALLS];

// fix15 balls_x_prev[MAX_BALLS];
// fix15 balls_y_prev[MAX_BALLS];

// Peg
struct peg {
  fix15 x;
  fix15 y;
} typedef peg_t;

// Start near the top
const int base_peg_y = 40;
fix15 peg_radius = int2fix15(6);
int peg_radius_int = 6;
int bottom_left_peg_x;

// Pegs
#define PEG_ROWS 16
#define NUM_PEGS ((PEG_ROWS) * (PEG_ROWS + 1) / 2)
peg_t pegs[NUM_PEGS];
int pegs_to_check[NUM_PEGS];
// int pegs_to_check2d[PEG_ROWS][PEG_ROWS];

fix15 distance;
fix15 normal_x;
fix15 normal_y;

// Wall detection
#define hitBottom(b) (b > int2fix15(board_bottom))
#define hitTop(b) (b < int2fix15(top_side))
#define hitLeft(a) (a < int2fix15(left_side))
#define hitRight(a) (a > int2fix15(right_side))

// Histogram information
#define HIST_BAR_COUNT PEG_ROWS + 1

// Constant values, filled in main
int histogram_height;
int board_bottom;
int histogram_bars_left_side[HIST_BAR_COUNT];
int histogram_widths[HIST_BAR_COUNT];

// Updated during the program
int histogram_max = 0;
int histogram[HIST_BAR_COUNT];
int histogram_bar_heights[HIST_BAR_COUNT];
int current_box;
int current_row;

void draw_histogram_frame() {
  for (int i = 0; i < HIST_BAR_COUNT; i++) {
    if (histogram[i] > histogram[histogram_max]) {
      histogram_max = i;
    }
  }
  for (int i = 0; i < HIST_BAR_COUNT; i++) {
    drawHLine(histogram_bars_left_side[i],
              bottom_side - histogram_bar_heights[i], histogram_widths[i],
              bkgnd_color); // Remove previous bar top line

    // update with new scaling factor
    histogram_bar_heights[i] = fix82int(multfix8(
        int2fix8(histogram_height),
        divfix8(int2fix8(histogram[i]), int2fix8(histogram[histogram_max]))));

    drawHLine(histogram_bars_left_side[i],
              bottom_side - histogram_bar_heights[i], histogram_widths[i],
              GREEN); // Draw the updated bar top line
  }
}

// Create a ball
void spawn_ball(ball_t *ball) {
  ball->x = int2fix15(middle_x + 1);
  ball->y =
      int2fix15(top_side) + ball_radius; // just below the top of the screen
  // Random horizontal velocity in range ~[-0.1 , 0.1]
  fix15 rand_val = get_rand_32(); // Random number in whole range of fix15
  rand_val %= (6u << 11);
  rand_val -= (3u << 11);
  ball->vx = rand_val;
  if (ball->vx == 0) {
    ball->vx += 1; // Add tiny value to prevent stuck balls (if vx = 0)
  }
  // Moving down
  ball->vy = int2fix15(0);
}

// Detect wallstrikes, update velocity and position
void check_walls(ball_t *ball) {
  // Reverse direction if we've hit a wall
  // Let the ball go a little off the top

  if (hitBottom(ball->y)) {
    // Reset ball to starting position
    // So just shift the x position right before dividing
    current_box =
        (fix152int(ball->x) + (PEG_H_SEP - bottom_left_peg_x + 1)) / PEG_H_SEP;
    histogram[current_box]++;

    // balls_dropped++; // Keep track of total
    spawn_ball(ball);
    return; // don’t apply further wall logic
  }
  if (hitRight(ball->x)) {
    ball->vx = (-ball->vx);
    ball->x = (ball->x - int2fix15(5));
    return;
  }
  if (hitLeft(ball->x)) {
    ball->vx = (-ball->vx);
    ball->x = (ball->x + int2fix15(5));
    return;
  }
}
// Track last peg hit
// static int last_peg = -1; // -1 means none yet

const fix15 alpha = float2fix15(0.960433870103);
const fix15 beta = float2fix15(0.397824734759);

fix15 alpha_max_beta_min(const fix15 a, const fix15 b) {
  fix15 abs_a = absfix15(a);
  fix15 abs_b = absfix15(b);
  return multfix15(alpha, MAX(abs_a, abs_b)) +
         multfix15(beta, MIN(abs_a, abs_b));
}

// Check collision of ball with peg, update velocity/position
void check_peg_collision(ball_t *ball, int length) {

  static fix15 peg_x;
  static fix15 peg_y;
  static int i;
  static int istart;
  static int temp_y;

  temp_y = fix152int(ball->y); // take the current ball y position
  if (temp_y >= 40) { // check if the ball is at least past the first row of
                      // pegs (the topmost peg)
    current_row = ((temp_y - base_peg_y + PEG_V_SEP) /
                   PEG_V_SEP); // calculate the current row
    if (ball->vx <=
        0) { // negative velocity means the ball is travelling towards the top
             // of the screen, need only check the pegs in the row above
      istart = ((current_row - 1) * current_row) / 2;
    } else { // positive velocity means the ball is travelling towards the
             // bottom of the screen, check the current row
      istart = (current_row * (current_row + 1)) / 2;
    }
  } else {
    current_row = 0;
    istart = 0;
    // only check pin index 0, i=0, i<1
  }

  // current_box =
  //       (fix152int(ball->x) + (PEG_H_SEP - bottom_left_peg_x + 1)) /
  //       PEG_H_SEP;

  for (i = istart; i < (istart + current_row + 1) && i < NUM_PEGS; i++) {
    peg_x = pegs[i].x;
    peg_y = pegs[i].y;

    fix15 dx = ball->x - peg_x;
    fix15 dy = ball->y - peg_y;
    // Quick bounding box rejection
    fix15 radius_sum = ball_radius + peg_radius;
    if (absfix15(dx) >= radius_sum || absfix15(dy) >= radius_sum) {
      continue; // no collision
    }

    // Distance (convert back to fix15)
    // Fast approximation of sqrt(dx^2+dy^2)
    fix15 distance = alpha_max_beta_min(dx, dy);
    // fix15 distance = alpha_max_beta_min(dx,dy);

    if (distance == 0) {
      // Avoid divide by zero (just nudge ball up)
      distance = int2fix15(1);
      dy = int2fix15(1);
    }

    // Normal vector (peg -> ball)
    fix15 normal_x = divfix15(dx, distance);
    fix15 normal_y = divfix15(dy, distance);

    // Projection of velocity on normal
    fix15 dot = multfix15(normal_x, ball->vx) + multfix15(normal_y, ball->vy);
    fix15 inter = -2 * dot;

    // Only if moving toward peg
    if (inter > 0) {

      // Push ball just outside peg
      ball->x = peg_x + multfix15(normal_x, (radius_sum + int2fix15(1)));
      ball->y = peg_y + multfix15(normal_y, (radius_sum + int2fix15(1)));

      // Reflect velocity
      ball->vx = ball->vx + multfix15(normal_x, inter);
      ball->vy = ball->vy + multfix15(normal_y, inter);

      // Hit a new peg
      if (i != ball->last_peg) {
        ball->last_peg = i;
        play_waveform_once();

        // Apply bounciness energy loss
        ball->vx = multfix15(ball->vx, bounciness);
        ball->vy = multfix15(ball->vy, bounciness);
      }

      // Return early (already hit a peg)
      return;
    }
  }
}

void position_update(ball_t *ball) {
  // if (abs(ball->vx) > vmax) {
  //   ball->vx = ball->vx > 0 ? vmax : -vmax;
  // }
  // if (abs(ball->vy) > vmax) {
  //   ball->vy = ball->vy > 0 ? vmax : -vmax;
  // }
  // Update position using velocity
  ball->x = ball->x + ball->vx;
  // positions_arr[0][count] = fix152float(*x);
  ball->y = ball->y + ball->vy;
  // positions_arr[1][count] = fix152float(*y);
  // count++;
}

void draw_ball(ball_t *ball, short color) {
  static int ball_x;
  ball_x = fix152int(ball->x);
  static int ball_y;
  ball_y = fix152int(ball->y);
  fillCircle(fix152int(ball->x), fix152int(ball->y), ball_radius_int, color);
  // drawPixel(ball_x, ball_y, color);         // top left
  // drawPixel(ball_x, ball_y + 1, color);     // bottom left
  // drawPixel(ball_x + 1, ball_y, color);     // top right
  // drawPixel(ball_x + 1, ball_y + 1, color); // bottom right
}

void redraw_text() {
  float gf = fix152float(gravity);
  float bf = fix152float(bounciness);
  // Parameters

  sprintf(num_text, num_format, num_balls);

  sprintf(grav_text, grav_format, gf);

  sprintf(bounce_text, bounce_format, bf);

  sprintf(time_text, time_format, PT_GET_TIME_usec() / 1000000);
  balls_dropped = num_balls;
  for (int hist_bar = 0; hist_bar < HIST_BAR_COUNT; hist_bar++) {
    balls_dropped += histogram[hist_bar];
  }
  sprintf(total_text, total_format, balls_dropped);

  clearRect(PARAM_TEXT_LEFT, PARAM_TEXT_TOP, PARAM_TEXT_RIGHT,
            PARAM_TEXT_BOTTOM, bkgnd_color);

  // Basic info
  setCursor(10, 5);
  writeString("Raspberry Pi Pico");
  setCursor(10, 15);
  writeString("Digital Galton Board");

  setCursor(10, 30);
  if (current_param == NUM_BALLS_PARAM) {
    setTextColor2(BLACK, WHITE);
  } else {
    setTextColor(WHITE);
  }
  writeString(num_text);

  setCursor(10, 40);
  if (current_param == GRAVITY_PARAM) {
    setTextColor2(BLACK, WHITE);
  } else {
    setTextColor(WHITE);
  }
  writeString(grav_text);

  setCursor(10, 50);
  if (current_param == BOUNCE_PARAM) {
    setTextColor2(BLACK, WHITE);
  } else {
    setTextColor(WHITE);
  }
  writeString(bounce_text);

  setTextColor(WHITE);

  // Stats
  setCursor(10, 65);
  writeString(time_text);
  setCursor(10, 75);
  writeString(total_text);
}

void reset_histogram() {
  for (int hist_bar = 0; hist_bar < HIST_BAR_COUNT; hist_bar++) {
    histogram[hist_bar] = 0;
    histogram_bar_heights[hist_bar] = 0;
  }
}

// ==================================================
// === graphics demo -- RUNNING on core 0
// ==================================================
static PT_THREAD(protothread_graphics(struct pt *pt)) {

  PT_BEGIN(pt);
  // the protothreads interval timer
  PT_INTERVAL_INIT();

  static struct pt child;

  // for displaying the balls
  static int time = 0;
  balls_dropped = num_balls;
  text_update_f = false;

  // Variables for maintaining frame rate
  static uint32_t begin_time;
  static uint32_t draw_time;

  // position of the display primitives
  static short peg_color = GREEN;
  static short ball_color = RED;

  // Clear screen
  clearLowFrame(0, 0);

  // Text bounding box
  drawHLine(PARAM_TEXT_LEFT, PARAM_TEXT_TOP, PARAM_TEXT_RIGHT, WHITE);
  drawHLine(PARAM_TEXT_LEFT, PARAM_TEXT_BOTTOM, PARAM_TEXT_RIGHT, WHITE);
  drawVLine(PARAM_TEXT_LEFT, PARAM_TEXT_TOP, PARAM_TEXT_BOTTOM, WHITE);
  drawVLine(PARAM_TEXT_RIGHT, PARAM_TEXT_TOP, PARAM_TEXT_BOTTOM, WHITE);

  // // Screen bounding box (TODO: REMOVE)
  // drawHLine(left_side + 5, top_side + 2, right_side, WHITE);
  // drawHLine(left_side + 5, bottom_side - 4, right_side, WHITE);
  // drawVLine(left_side + 5, top_side + 2, bottom_side, WHITE);
  // drawVLine(right_side - 4, top_side - 2, bottom_side, WHITE);

  redraw_text();

  // Spawn all the balls
  int i;
  for (i = 0; i < num_balls; i++) {
    spawn_ball(&balls[i]);
  }
  // Setup a 1Hz timer
  // static struct repeating_timer timer;
  ////add_repeating_timer_ms(-1000, repeating_timer_callback, NULL, &timer);

  static char frame_rate[8]; // 7 char string for displaying frame rate
  setTextColor2(WHITE, BLACK);
  setTextSize(1);

  static int bar_num;

  while (true) {
    draw_histogram_frame(); // Draw the histogram once per frame
    // restart logic
    if (restart_graphics) {
      // reset the flag
      reset_histogram();
      restart_graphics = false;
      // restarts the current thread at PT_BEGIN(pt);
      // as if it has not executed before
      PT_RESTART(pt);
    }

    // frame number
    time++;

    // this syncs the thread to the frame rate
    // while (gpio_get(VSYNC)) {};
    begin_time = PT_GET_TIME_usec();

    // Update text every 1/3 second
    if (time % 10 == 0 || text_update_f) {
      if (text_update_f) {
        text_update_f = false;
        clearLowFrame(0, 0);    // Clear the screen
        draw_histogram_frame(); // Draw the histogram once per frame
      }
      redraw_text();
      sprintf(frame_rate, "%5.2f",
              1.0f / (((float)draw_time) * RATE_CONVERSION));
      setTextColor2(WHITE, BLACK);
      setCursor(right_side - 50, top_side + 5);
      writeStringBig(frame_rate);
    }

    // DOES NOT CHECK for an edge, so very fast drawing
    // MAY need a 1 mSec yield at the end
    // PT_YIELD_UNTIL(pt, !gpio_get(VSYNC));
    // frame_start_time = PT_GET_TIME_usec();
    // clear takes 200 uSec
    // clearRect(left_side, top_side, right_side, bottom_side,
    // (short)bkgnd_color);

    // memcpy(balls_x_prev, balls_x, sizeof(fix15) * MAX_BALLS);
    // memcpy(balls_y_prev, balls_y, sizeof(fix15) * MAX_BALLS);

    // Apply physics and draw balls one at a time
    static ball_t *ball;
    for (i = 0; i < num_balls; i++) {
      ball = &balls[i];

      // Clear last ball
      draw_ball(ball, bkgnd_color);

      check_walls(ball);
      check_peg_collision(ball, NUM_PEGS);

      // Apply gravity
      ball->vy += gravity;
      // update ball's position and velocity
      position_update(ball);

      // Draw ball
      draw_ball(ball, ball_color);
    }

    for (int j = 0; j < NUM_PEGS; j++) {
      // Draw pegs
      drawCircle(fix152int(pegs[j].x), fix152int(pegs[j].y), peg_radius_int,
                 peg_color);
    }

    draw_time = PT_GET_TIME_usec() - begin_time;
    spare_time = FRAME_RATE - draw_time;
    //  delay in accordance with frame rate

    // Thread is paced by the Vsync edge
    if (spare_time > 0) {
      PT_YIELD_usec(spare_time);
    }
    // PT_YIELD(pt);
  }
  PT_END(pt);
} // graphics thread

// ==================================================
// === toggle25 thread on core 1
// ==================================================
// the on-board LED blinks
static PT_THREAD(protothread_toggle25(struct pt *pt)) {
  PT_BEGIN(pt);
  static bool LED_state = false;

  // set up LED p25 to blink
  gpio_init(25);
  gpio_set_dir(25, GPIO_OUT);
  gpio_put(25, true);
  // data structure for interval timer
  PT_INTERVAL_INIT();

  while (1) {
    // yield time 0.1 second
    // PT_YIELD_usec(100000) ;
    PT_YIELD_INTERVAL(100000);
    if (spare_time <= 0) {

      // toggle the LED on PICO
      LED_state = true;
    } else {
      LED_state = false;
    }
    gpio_put(25, LED_state);

    //
    // NEVER exit while
  } // END WHILE(1)
  PT_END(pt);
} // blink thread

// ==================================================
// === user's serial input thread on core 1
// ==================================================
// serial_read an serial_write do not block any thread
// except this one
static PT_THREAD(protothread_serial(struct pt *pt)) {
  PT_BEGIN(pt);

  while (1) {
    // print prompt
    sprintf(pt_serial_out_buffer,
            "reset (p)arams/(r)efresh/(n)um "
            "balls/(g)ravity/(b)ounciness/ball (s)ize/(m)ute/(v)max: ");
    // sprintf(pt_serial_out_buffer,
    //         "current_row %d \n\r", current_row);
    // spawn a thread to do the non-blocking write
    serial_write;

    // spawn a thread to do the non-blocking serial read
    serial_read;

    int new_num_balls = num_balls;
    float ng_float = 0.0f;
    fix15 new_gravity = gravity;
    float nb_float = 0.0f;
    fix15 new_bounce = bounciness;
    int new_ball_radius = ball_radius_int;
    float nv_float = 0.0f;
    fix15 new_vmax = vmax;
    bool reset = false;

    char opt = pt_serial_in_buffer[0];
    switch (opt) {
    case 'p':
      new_num_balls = DEFAULT_NUM_BALLS;
      new_ball_radius = DEFAULT_BALL_RADIUS;
      new_bounce = DEFAULT_BOUNCINESS;
      new_gravity = DEFAULT_GRAVITY;
      vmax = DEFAULT_VMAX;
    case 'r':
      text_update_f = true;
      reset = true;
      break;
    case 'n':
      sscanf(pt_serial_in_buffer + 1, "%d\r", &new_num_balls);
      text_update_f = true;
      break;
    case 'g':
      sscanf(pt_serial_in_buffer + 1, "%f\r", &ng_float);
      new_gravity = float2fix15(ng_float);
      text_update_f = true;
      break;
    case 'b':
      sscanf(pt_serial_in_buffer + 1, "%f", &nb_float);
      new_bounce = float2fix15(nb_float);
      text_update_f = true;
      break;
    case 's':
      sscanf(pt_serial_in_buffer + 1, "%d\r", &new_ball_radius);
      text_update_f = true;
      break;
    case 'v':
      sscanf(pt_serial_in_buffer + 1, "%f\r", &nv_float);
      vmax = float2fix15(nv_float);
      text_update_f = true;
      break;
    case 'm':
      muted = !muted;
      break;
    default:
      sscanf(pt_serial_in_buffer + 1, "\r"); // Clear the read buffer
      break;
    }

    if (text_update_f || reset) {
      if (new_num_balls > num_balls) {
        for (int i = num_balls; i < new_num_balls; i++) {
          spawn_ball(&balls[i]);
        }
      }

      num_balls = new_num_balls;
      gravity = new_gravity;
      bounciness = new_bounce;
      ball_radius_int = new_ball_radius;
      ball_radius = int2fix15(ball_radius_int);

      restart_graphics = reset; // Signal a full restart
    }

    // // NEVER exit while

    // // sprintf(pt_serial_out_buffer, "Draw Time %4.1f mSec\n\r",
    // //         (float)(PT_GET_TIME_usec() - e_time) / 1000);
    // // sprintf(pt_serial_out_buffer, "Spare time: %d uSec\n\r", spare_time);
    // // spawn a thread to do the non-blocking write
    // // serial_write;

    // // PT_YIELD(pt);
    // //  NEVER exit while
  } // END WHILE(1)
  PT_END(pt);
} // serial thread

static PT_THREAD(protothread_input(struct pt *pt)) {
  PT_BEGIN(pt);

  adc_init();
  adc_gpio_init(GPIO_POT);
  adc_select_input(ADC_NUM_POT);

  gpio_init(GPIO_BUTTON);
  gpio_set_dir(GPIO_BUTTON, GPIO_IN);
  gpio_set_pulls(GPIO_BUTTON, false, true);
  debounce_state = NOT_PRESSED;
  possible = false;

  const unsigned int low_deadzone = 0x000f;
  const unsigned int high_deadzone = 0x0ff0;

  const unsigned int res_pow2 = 4;
  const unsigned int pot_factor = (high_deadzone - low_deadzone) >> res_pow2;

  const unsigned int ball_factor = MAX_BALLS / pot_factor;
  const fix15 grav_factor = float2fix15(0.05);
  const fix15 bounce_factor = float2fix15(0.05);

  static uint16_t pot_val = 0;
  static uint16_t old_pot_val = 0;
  static unsigned int new_num_balls = 0;
  static fix15 new_gravity = 0;
  static fix15 new_bounce = 0;

  while (1) {

    int i = gpio_get(GPIO_BUTTON);

    switch (debounce_state) {
    case PRESSED:
      if (i != possible) {
        debounce_state = MAYBE_NOT_PRESSED;
      }
      break;
    case MAYBE_PRESSED: // Update on MAYBE->PRESSED
      if (i == possible) {
        current_param = (current_param + 1) % PARAM_MOD;
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
    // const float conversion_factor = 3.3f / (1 << 12);
    pot_val = adc_read();
    if (pot_val < low_deadzone) {
      pot_val = low_deadzone;
    }
    if (pot_val > high_deadzone) {
      pot_val = high_deadzone;
    }

    pot_val = (pot_val - low_deadzone) >> res_pow2;

    if (current_param != NO_PARAM && (abs(pot_val - old_pot_val) > 1)) {
      switch (current_param) {
      case NUM_BALLS_PARAM:
        new_num_balls = pot_val * ball_factor;
        new_num_balls = MAX(1, new_num_balls);
        new_num_balls = MIN(MAX_BALLS - 1, new_num_balls);

        if (new_num_balls > num_balls) {
          for (int i = num_balls; i < new_num_balls; i++) {
            spawn_ball(&balls[i]);
          }
        }
        num_balls = new_num_balls;
        break;
      case GRAVITY_PARAM:
        new_gravity = multfix15(int2fix15(pot_val), grav_factor);
        gravity = new_gravity;
        break;
      case BOUNCE_PARAM:
        new_bounce = multfix15(int2fix15(pot_val), bounce_factor);
        bounciness = new_bounce;
        break;
      case NO_PARAM:
      default:
        break;
      }

      old_pot_val = pot_val;
      text_update_f = true;
    }

    PT_YIELD_usec(100000);
  }

  PT_END(pt);
}

// ========================================
// === core 1 main -- started in main below
// ========================================
void core1_main() {

  // announce the threader version on system reset
  printf("\n\rProtothreads RP2040/2350 v1.4 \n\r");
  printf("CORE 1\n\r");

  //
  //  === add threads  ====================
  // for core 1
  pt_add_thread(protothread_serial);
  pt_add_thread(protothread_toggle25);
  pt_add_thread(protothread_input);
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
  set_sys_clock_khz(250000, true);

  // SERIAL //

  // VGA //

  // Initialize the VGA screen
  initVGA();
  // printf("video assumes cpu clock=%d MHz", rgb_vid_freq);

  // OTHER IO //

  // Initidalize stdio
  stdio_init_all();

  // SPI & DMA TO DAC //

  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(SPI_PORT, 20000000);

  // Format SPI channel (channel, data bits per transfer, polarity, phase,
  // order)
  spi_set_format(SPI_PORT, 16, 0, 0, 0);

  // Map SPI signals to GPIO ports, acts like framed SPI with this CS mapping
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

  // Build sine table and DAC data table
  int i;
  int j;
  for (i = 0, j = 0; i < (sine_table_size); i++, j += 2) {
    raw_sin[i] = (int)(2047 * sin((float)i * 8.283 / (float)sine_table_size) +
                       2047); // 12 bit

    // Copy SPI message for chan A & B for "stereo" sound
    DAC_data[j] = DAC_config_chan_A | (raw_sin[i] & 0x0fff);
    DAC_data[j + 1] = DAC_config_chan_B | (raw_sin[i] & 0x0fff);
  }
  fix15 current_amplitude_0 = 0;
  int attack_time = 32;
  int decay_time = 32;
  fix15 one = int2fix15(1);
  fix15 thirtytwo = int2fix15(32);
  fix15 attack_inc = divfix15(one, thirtytwo);
  fix15 decay_inc = attack_inc;
  for (i = 0; i < dac_table_size; i++) {
    if (i < attack_time) {
      current_amplitude_0 = (current_amplitude_0 + attack_inc);
    }
    // Ramp down amplitude
    else if (i > dac_table_size - decay_time) {
      current_amplitude_0 = (current_amplitude_0 - decay_inc);
    }
  }

  // Select DMA channels
  data_chan = dma_claim_unused_channel(true);
  ctrl_chan = dma_claim_unused_channel(true);

  // Setup the control channel
  c = dma_channel_get_default_config(ctrl_chan);          // default configs
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32); // 32-bit txfers
  channel_config_set_read_increment(&c, false);  // no read incrementing
  channel_config_set_write_increment(&c, false); // no write incrementing
  channel_config_set_chain_to(&c, data_chan);    // chain to data channel

  dma_channel_configure(
      ctrl_chan, // Channel to be configured
      &c,        // The configuration we just created
      &dma_hw->ch[data_chan]
           .read_addr,      // Write address (data channel read address)
      &dac_address_pointer, // Read address (POINTER TO AN ADDRESS)
      1,                    // Number of transfers
      false                 // Don't start immediately
  );

  // Setup the data channel
  c2 = dma_channel_get_default_config(data_chan);          // Default configs
  channel_config_set_transfer_data_size(&c2, DMA_SIZE_16); // 16-bit txfers
  channel_config_set_read_increment(&c2, true);   // yes read incrementing
  channel_config_set_write_increment(&c2, false); // no write incrementing
  // Double the pacing from the demo (2 writes instead of one)
  dma_timer_set_fraction(0, 0x002e, 0xffff);
  // 0x3b means timer0 (see SDK manual)
  channel_config_set_dreq(&c2, 0x3b); // DREQ paced by timer 0
  // DONT chain to the controller DMA channel so we can play manually

  dma_channel_configure(
      data_chan,                 // Channel to be configured
      &c2,                       // The configuration we just created
      &spi_get_hw(SPI_PORT)->dr, // write address (SPI data register)
      DAC_data,                  // The initial read address
      dac_table_size,            // Number of transfers
      false                      // Don't start immediately.
  );

  // PHYSICS/GRAPHICS OBJECTS //
  for (i = 0; i < MAX_BALLS; i++) {
    balls[i].x = 0;
    balls[i].y = 0;
    balls[i].vx = 0;
    balls[i].vy = 0;
    balls[i].last_peg = -1;
  }

  int row;
  int col;
  int peg_x;
  int peg_y;
  int peg_num = 0;
  histogram_bars_left_side[0] = 0;
  int last_x = 0;
  for (row = 0; row < PEG_ROWS; row++) {
    for (col = 0; col <= row; col++) {
      peg_x = middle_x - (PEG_H_SEP * row) / 2 + (PEG_H_SEP * col);
      peg_y = base_peg_y + row * PEG_V_SEP;

      pegs[peg_num].x = int2fix15(peg_x);
      pegs[peg_num].y = int2fix15(peg_y);
      pegs_to_check[peg_num] = peg_num;
      peg_num++;
      if (row == PEG_ROWS - 1) {
        if (col == 0) {
          bottom_left_peg_x = peg_x;
        }
        histogram_bars_left_side[col + 1] = peg_x;
        histogram_widths[col] = peg_x - last_x;
        last_x = peg_x;
      }
    }
  }
  histogram_widths[16] = right_side - last_x;
  peg_num--;
  // Bottom of the board
  board_bottom = fix152int(pegs[peg_num].y) + peg_radius_int;
  // base_peg_y + (16*PEG_V_SEP) + peg_radius_int = 40 + 16 * 19 + 4 = 350

  // Histogram is from bottom_side to board_bottom
  histogram_height = bottom_side - board_bottom - 33; // set to 96

  reset_histogram();

  // start core 1 threads
  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // === config threads ========================
  // for core 0
  pt_add_thread(protothread_graphics);
  //
  // === initalize the scheduler ===============
  pt_schedule_start;
  // NEVER exits
  // ===========================================
} // end main
