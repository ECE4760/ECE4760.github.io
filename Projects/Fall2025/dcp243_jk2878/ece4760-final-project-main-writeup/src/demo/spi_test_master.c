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

#include "snake_spi.h"

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


/////////////////////////////// GRAPHICS ///////////////////////////////////////

// uS per frame
#define FRAME_RATE 33333
#define RATE_CONVERSION 0.000001f

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
char time_text[sizeof(time_format) + 16];

char out_msg_buffer[255];
char in_msg_buffer[255];

// Screen size parameters
const int bottom_side = 479;
const int top_side = 0;
const int SCREEN_HEIGHT = bottom_side - top_side;
const int left_side = 0;
const int right_side = 639;
const int SCREEN_WIDTH = right_side - left_side;
const int middle_x = SCREEN_WIDTH / 2;

//////////////////////////////// SPI //////////////////////////////////////////

static PT_THREAD(redraw_text(struct pt *pt)) {
  PT_BEGIN(pt);

  // Parameters

  sprintf(time_text, time_format, PT_GET_TIME_usec() / 1000000);

  clearRect(PARAM_TEXT_LEFT, PARAM_TEXT_TOP, PARAM_TEXT_RIGHT,
            PARAM_TEXT_BOTTOM, bkgnd_color);

  // Basic info
  setCursor(10, 5);
  writeString("Raspberry Pi Pico");
  setCursor(10, 15);
  writeString("SPI TEST");

  setCursor(10, 30);
  setTextColor(WHITE);

  // Stats
  setCursor(10, 40);
  writeString(time_text);

  PT_EXIT(pt);
  PT_END(pt);
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
  text_update_f = false;

  // Variables for maintaining frame rate
  static uint32_t begin_time;
  static uint32_t draw_time;

  // Clear screen
  clearLowFrame(0, 0);

  // Text bounding box
  drawHLine(PARAM_TEXT_LEFT, PARAM_TEXT_TOP, PARAM_TEXT_RIGHT, WHITE);
  drawHLine(PARAM_TEXT_LEFT, PARAM_TEXT_BOTTOM, PARAM_TEXT_RIGHT, WHITE);
  drawVLine(PARAM_TEXT_LEFT, PARAM_TEXT_TOP, PARAM_TEXT_BOTTOM, WHITE);
  drawVLine(PARAM_TEXT_RIGHT, PARAM_TEXT_TOP, PARAM_TEXT_BOTTOM, WHITE);

  // Screen bounding box (TODO: REMOVE)
  drawHLine(left_side + 5, top_side + 2, right_side, WHITE);
  drawHLine(left_side + 5, bottom_side - 4, right_side, WHITE);
  drawVLine(left_side + 5, top_side + 2, bottom_side, WHITE);
  drawVLine(right_side - 4, top_side - 2, bottom_side, WHITE);

  PT_SPAWN(pt, &child, redraw_text(&child));

  static char frame_rate[8]; // 7 char string for displaying frame rate
  setTextColor2(WHITE, BLACK);
  setTextSize(1);

  static int bar_num;

  while (true) {
    // restart logic
    if (restart_graphics) {
      // reset the flag
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
    if (1) {
      // if (text_update_f) {
      //   text_update_f = false;
      //   clearLowFrame(0, 0);    // Clear the screen
      // }

      PT_SPAWN(pt, &child, redraw_text(&child));
      sprintf(frame_rate, "%5.2f",
              1.0f / (((float)draw_time) * RATE_CONVERSION));
      setTextColor2(WHITE, BLACK);
      setCursor(right_side - 50, top_side + 5);
      writeStringBig(frame_rate);
    }

    // DOES NOT CHECK for an edge, so very fast drawing
    // MAY need a 1 mSec yield at the end
    PT_YIELD_UNTIL(pt, !gpio_get(VSYNC));
    // clear takes 200 uSec
    // clearRect(left_side, top_side, right_side, bottom_side,
    // (short)bkgnd_color);

    setCursor(right_side / 2, (bottom_side / 2) - 20);
    writeStringBig(out_msg_buffer);
    setCursor(right_side / 2, (bottom_side / 2) + 20);
    writeStringBig(in_msg_buffer);

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
// === user's serial input thread on core 1
// ==================================================
// serial_read an serial_write do not block any thread
// except this one
static PT_THREAD(protothread_serial(struct pt *pt)) {
  PT_BEGIN(pt);

  while (1) {
    // print prompt
    sprintf(pt_serial_out_buffer, "text to echo: ");
    serial_write;

    serial_read;
    memcpy(out_msg_buffer, pt_serial_in_buffer, sizeof(pt_serial_in_buffer));
    strcat(out_msg_buffer, "\n");
    spi_write_blocking(SV_SPI_PORT, out_msg_buffer, sizeof(out_msg_buffer));

    sprintf(pt_serial_out_buffer, "Read: %d bytes",
            spi_read_blocking(SV_SPI_PORT, 0, in_msg_buffer,
                              sizeof(in_msg_buffer)));
    serial_write;

    memcpy(pt_serial_out_buffer, in_msg_buffer, sizeof(in_msg_buffer));
    serial_write;
    PT_YIELD(pt);

  } // END WHILE(1)
  PT_END(pt);
} // serial thread

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

  // OTHER IO //

  // Initidalize stdio
  stdio_init_all();

  // SPI & DMA TO DAC //

  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(SV_SPI_PORT, SPI_BAUD);

  // Format SPI channel (channel, data bits per transfer, polarity, phase,
  // order)
  spi_set_format(SV_SPI_PORT, SPI_BITS, SPI_POL, SPI_PHASE, SPI_ORDER);

  // Set to master
  spi_set_slave(SV_SPI_PORT, false);

  // Map SPI signals to GPIO ports, acts like framed SPI with this CS mapping
  gpio_set_function(SV_SPI_MISO, GPIO_FUNC_SPI);
  gpio_set_function(SV_SPI_CS0, GPIO_FUNC_SPI);
  gpio_set_function(SV_SPI_SCK, GPIO_FUNC_SPI);
  gpio_set_function(SV_SPI_MOSI, GPIO_FUNC_SPI);

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
