/**
 * Hunter Adams (vha3@cornell.edu)
 *
 * HARDWARE CONNECTIONS
 *  - GPIO 16 ---> VGA Hsync
 *  - GPIO 17 ---> VGA Vsync
 *  - GPIO 18 ---> 330 ohm resistor ---> VGA Green lo-bit |__ both wired to 150 ohm to ground
 *  - GPIO 19 ---> 220 ohm resistor ---> VGA Green hi_bit |   and to VGA Green
 *  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
 *  - GPIO 21 ---> 330 ohm resistor ---> VGA Red
 *  - RP2040 GND ---> VGA GND
 *
 * RESOURCES USED
 *  - PIO state machines 0, 1, and 2 on PIO instance 0
 *  - DMA channels 0, 1, 2, and 3
 *  - 153.6 kBytes of RAM (for pixel color data)
 *
 * Protothreads v1.1.1
 * Serial console on GPIO 0 and 1 for debugging
 *
 * Joystick:
 * HiLetgo Game Joystick Sensor Game Controller --
 * https://www.amazon.com/dp/B00P7QBGD2?psc=1&ref=ppx_yo2ov_dt_b_product_details
 * joystick
 * pins:
 * GND ---> PICO GND
 * +5 ---> PICO +3
 * Rx ---> ADC channel 0 GPIO 26
 * Ry ---> ADC channel 0 GPIO 27
 * SW ---> GPIO 4 with pullup turned on
 *
 * DAC :
 * GPIO 5 (pin 7) Chip select
   GPIO 6 (pin 9) SCK/spi0_sclk
   GPIO 7 (pin 10) MOSI/spi0_tx
   3.3v (pin 36) -> VCC on DAC
   GND (pin 3)  -> GND on DAC
 *
 * =====================================================
 *
 * This is a BIG example, with lots of moving parts, so it needs a software outline!
 * There are several support routines for the cursor, menu, and video.
 * There are two threads running on each core, plus the synthesis ISR running on core1
 *
 * ==========
 * Support routines:
 * -- ADC setup
 * -- Cursor erase and cursor draw.
 *      These have to save and restore actual image pixels with every cursor move
 * -- Define menu item structure
 * -- Draw the menu
 * -- Modify the menu based on user input
 *      Supports both joystick input and serial input.
 * -- Define two different fixed point schemes
 *      One for DDS which requires bigger numbers, the other for FFT which needs fast execution.
 * -- Set up a low-level timer alarm which executes quickl
 * -- Define the FFT routine in 16-bit fixed point for speed
 * -- Define routine to extract a fast FFT magnutude specterum
 *      Accuracy is about 2%
 * -- Define routine to extract a VERY low resoultion approximaton to log(mag)
 *      The bit twiddling produces a u4x4 fixed poiint log. Good enough for graphics only.
 *
 * ==========
 * Core0:
 *
 * -- Graphics thread which generates and handles the menu.
 * This is the GUI 'event loop'. Every 16 mSEc it checks all human inputs and decied what to do.
 * ---- Runs the ADC to get joystick positions
 * ---- Handles timing for single and double-clicks of the joystick button
 * ---- Turns on serial input when a double-cllick is detected in a menu item
 * ---- Handles the graphics cursor drawing
 * ---- Defines menu items, highlights them for change,  and changes them
 *
 * -- Blinky heart beat -- just blinks, unless the threader crashes
 *
 * ==========
 * Core1:
 *
 * -- synthesis ISR triggered by a timer alarm
 * ---- runs at 40KHz (25 uSec interval) for audio synthesis.
 * ---- Worst case execution time is about 5 uSec.
 * ---- Computes DDS to get modulating wave, then computes output wave
 * ---- Pushes output to an SPI channel
 * ---- Fills an array with DDS results for the FFT thread
 *
 * -- FFT thread which waits for a buffer full signal from the ISR, then draws spectra
 * ---- Does a 2048 point FFT then gets the amplitude of the frequency bins and takes a VERY approx log2
 * ---- plots the amlitude spectrum, log amplitude spectrum, and spectrogram
 *
 * -- FM synthesis control thread which sequences the synthesis ISR
 * ---- precomputes a bunch of fixed point constants to amke the ISR faster
 * ---- sequences a C scale of 8 notes.
 *
 * PIO: There are three PIO state machines that generate the video.
 * See: https://vanhunteradams.com/Pico/VGA/VGA.html
 */
// ==========================================
// === VGA graphics library
// ==========================================
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "vga16_graphics.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

// // Our assembled programs:
// // Each gets the name <pio_filename.pio.h>
// #include "hsync.pio.h"
// #include "vsync.pio.h"
// #include "rgb.pio.h"

// ==========================================
// === hardware and protothreads globals
// ==========================================
#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/multicore.h"
#include "string.h"
// protothreads header
#include "pt_cornell_rp2040_v1_1_1.h"

// globals for the graphincs
short adc_x, adc_y;
short draw_x, draw_y, last_draw_x, last_draw_y, last_click_x, last_click_y,
    x_latch;
short click, double_click, button, last_button,
    click_latch; //, double_click_latch ;
short first_click = 1;
int   click_time, last_click_time;
int   clicked_item;
float serial_value;
char  serial_value_input[32];
bool  printParams    = true;
bool  modifiedParams = true;

// ===========================================
// ADC setup
// ===========================================
// setup ADC
static float conversion_factor = 3.3f / (1 << 12);

void ADC_setup(void)
{
  adc_init();
  // Make sure GPIO is high-impedance, no pullups etc
  // x
  // adc_gpio_init(26);
  // y
  // adc_gpio_init(27);
  adc_gpio_init(28);
  adc_select_input(2);
}

// ===========================================
// Cursor
// ===========================================
short cursor_pixel_x[5], cursor_pixel_y[5];

// Erase cursor
// replace cursor color with saved image colors
void erase_cursor(short x, short y)
{
  // first the x direction, then y
  for (int i = 0; i < 5; i++)
  {
    drawPixel(x - 2 + i, y, cursor_pixel_x[i]);
    drawPixel(x, y - 2 + i, cursor_pixel_y[i]);
  }
}

// Draw cursor
// replace image with cursor color and save image pixels
void draw_cursor(short x, short y)
{
  for (int i = 0; i < 5; i++)
  {
    cursor_pixel_x[i] = readPixel(x - 2 + i, y);
    cursor_pixel_y[i] = readPixel(x, y - 2 + i);
    drawPixel(x - 2 + i, y, WHITE);
    drawPixel(x, y - 2 + i, WHITE);
  }
}

// ==================================================
// === set up a menu scheme for use with VGA
// ==================================================
// There is one list of up to 16 items drawn down the left side of the screen
// ALL items store and update using float values
// int values are copied from the float
struct menu_item
{
  // displayed string
  char item_name[15];
  // displayed color
  int item_color;
  //1=float 0=int ;
  int item_data_type;
  // 1=log 0=linear
  int item_inc_type;
  // increment delta if linear, ratio if log
  float item_increment;
  // the current value, min, max
  // al updates are performed on float, then copied to int
  int   item_int_value;
  float item_float_value;
  float item_float_min;
  float item_float_max;
};

// ======
// build the menu array of items
struct menu_item menu[16];
// Actually use 11 in this example
#define menu_length 11
//
void draw_item(short menu_x, short menu_y, int item_index, int menu_num_items)
{
  if (item_index < menu_num_items)
  {
    // display the name
    setTextColor(menu[item_index].item_color);
    setCursor(menu_x, menu_y + item_index * 25);
    setTextSize(1);
    writeString(menu[item_index].item_name);
    // display the value
    setTextColor(WHITE);
    fillRect(menu_x, menu_y + item_index * 25 + 10, 90, 11, BLACK);
    setCursor(menu_x, menu_y + item_index * 25 + 11);
    char value_str[15];
    // copy float to int value
    menu[item_index].item_int_value = (int)menu[item_index].item_float_value;
    // get the data type in bit 0
    if (menu[item_index].item_data_type == 0)
      sprintf(value_str, "%d", menu[item_index].item_int_value);
    if (menu[item_index].item_data_type == 1)
      sprintf(value_str, "%8.3f", menu[item_index].item_float_value);
    writeString(value_str);
  }
}

// =====
void draw_menu(short menu_x, short menu_y, int menu_num_items)
{
  for (int i = 0; i < menu_num_items; i++)
  {
    draw_item(menu_x, menu_y, i, menu_num_items);
  }
}

// ===== change value with joystick
// direction is 1 for increase,  -1 for decrease
void change_value(int index, int direction)
{
  if (direction > 0)
  {
    if (menu[index].item_inc_type == 0)
      menu[index].item_float_value +=
          menu[index].item_increment * abs(direction);
    if (menu[index].item_inc_type == 1)
      menu[index].item_float_value *=
          menu[index].item_increment * abs(direction);
  }
  if (direction < 0)
  {
    if (menu[index].item_inc_type == 0)
      menu[index].item_float_value -=
          menu[index].item_increment * abs(direction);
    if (menu[index].item_inc_type == 1)
      menu[index].item_float_value /=
          menu[index].item_increment * abs(direction);
  }
  // check min/max
  if (menu[index].item_float_value > menu[index].item_float_max)
    menu[index].item_float_value = menu[index].item_float_max;
  if (menu[index].item_float_value < menu[index].item_float_min)
    menu[index].item_float_value = menu[index].item_float_min;
  // update integer to match
  menu[index].item_int_value = (int)menu[index].item_float_value;
}

// ===== change value with serial
// direction is 1 for increase,  -1 for decrease
void change_value_serial(int index, float value)
{
  menu[index].item_float_value = value;
  // check min/max
  if (menu[index].item_float_value > menu[index].item_float_max)
    menu[index].item_float_value = menu[index].item_float_max;
  if (menu[index].item_float_value < menu[index].item_float_min)
    menu[index].item_float_value = menu[index].item_float_min;
  // update integer to match
  menu[index].item_int_value = (int)menu[index].item_float_value;
}

// ==========================================
// === fixed point s19x12 for DDS
// ==========================================
// s19x12 fixed point macros == for DDS
typedef signed int fix;
#define mul(a, b)                                                              \
  ((fix)((((signed long long)(a)) * ((signed long long)(b))) >>                \
         12))                               //multiply two fixed 16:16
#define float_to_fix(a) ((fix)((a)*4096.0)) // 2^12
#define fix_to_float(a) ((float)(a) / 4096.0)
#define fix_to_int(a)   ((int)((a) >> 12))
#define int_to_fix(a)   ((fix)((a) << 12))
#define div(a, b)       ((fix)((((signed long long)(a) << 12) / (b))))
#define absfix(a)       abs(a)

// ==========================================
// === fixed point s1x14 for FFT
// ==========================================
// s1.14 format -- short format is faster
// == resolution 2^-14 = 6.1035e-5
// == dynamic range is +1.9999/-2.0
typedef signed short s1x14;
#define muls1x14(a, b)    ((s1x14)((((int)(a)) * ((int)(b))) >> 14))
#define float_to_s1x14(a) ((s1x14)((a)*16384.0)) // 2^14
#define s1x14_to_float(a) ((float)(a) / 16384.0)
#define abss1x14(a)       abs(a)
#define divs1x14(a, b)    ((s1x14)((((signed int)(a) << 14) / (b))))
// shift 12 bits into 14 bits so full scale dds is about 0.25
#define dds_to_s1x14(a)   ((s1x14)((a) >> 14))

// ==========================================
// === set up SPI DAC
// ==========================================
// All SPI DAC setup was gotten from HUnter Adams
// https://vanhunteradams.com/Pico/TimerIRQ/SPI_DDS.html
// DAC parameters
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

//SPI configurations
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define SPI_PORT spi0

#define MUX_SEL_A 12
#define MUX_SEL_B 11
#define MUX_SEL_C 10
static int   mux_select = 0;
static float voltage    = 3;

// data for the spi port
uint16_t DAC_data;

// ==========================================
// === set up DDS and timer ISR
// ==========================================
// 1/Fs in microseconds
volatile int alarm_period = 25;

// DDS variables
#define NUM_KEYS       3
#define VOLTAGE_CUTOFF 1.0

unsigned int mod_inc[NUM_KEYS], main_inc[NUM_KEYS];
unsigned int current_mod_inc[NUM_KEYS], current_main_inc[NUM_KEYS];
unsigned int mod_accum[NUM_KEYS], main_accum[NUM_KEYS];

// amplitude paramters
fix max_mod_depth;
fix current_mod_depth[NUM_KEYS];
// waveform amplities -- must fit in +/-11 bits for DAC
fix max_amp = float_to_fix(1000.0);
fix current_amp[NUM_KEYS];

// timing in seconds
static volatile fix attack_time, mod_attack_time;
static volatile fix decay_time, mod_decay_time, recip_decay_time;
static volatile fix sustain_time, mod_sustain_time;
fix                 onefix = int_to_fix(1);
// internal timing in samples
fix                 note_time[NUM_KEYS];
static volatile fix attack_inc, decay_inc, mod_attack_inc, mod_decay_inc;
//float f_quad_decay_inc ;
// sine waves
fix sine_table[256];
fix mod_wave[NUM_KEYS], main_wave[NUM_KEYS];
// inputs
float Fs, Fmod;
//              C3     D3     E3     F3     G3     A3     B3     C4
float notes[8] = {130.8, 146.8, 164.8, 174.6, 196.0, 220.0, 246.9, 261.6};
//
int  note_start[NUM_KEYS], play_note[NUM_KEYS], add_delay[NUM_KEYS];
bool pressed[NUM_KEYS], prev_pressed[NUM_KEYS];
int  linear_dk = 0;
int  octave_num;

// ==========================================
// === set up timer ISR  used in this pgm
// ==========================================
// === timer alarm ========================
// !! modifiying alarm zero trashes the cpu
//        and causes LED  4 long - 4 short
// !! DO NOT USE alarm 0
// This low-level setup is ocnsiderably faster to execute
// than the hogh-level callback

#define ALARM_NUM 1
#define ALARM_IRQ TIMER_IRQ_1
// ISR interval will be 10 uSec
//
// the actual ISR
void compute_sample(void);
//
static void alarm_irq(void)
{
  // mark ISR entry
  gpio_put(2, 1);
  // Clear the alarm irq
  hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);
  // arm the next interrupt
  // Write the lower 32 bits of the target time to the alarm to arm it
  timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + alarm_period;
  //
  compute_sample();

  // mark ISR exit
  gpio_put(2, 0);
}

// set up the timer alarm ISR
static void alarm_in_us(uint32_t delay_us)
{
  // Enable the interrupt for our alarm (the timer outputs 4 alarm irqs)
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
  // Set irq handler for alarm irq
  irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
  // Enable the alarm irq
  irq_set_enabled(ALARM_IRQ, true);
  // Enable interrupt in block and at processor
  // Alarm is only 32 bits
  uint64_t target            = timer_hw->timerawl + delay_us;
  // Write the lower 32 bits of the target time to the alarm which
  // will arm it
  timer_hw->alarm[ALARM_NUM] = (uint32_t)target;
}

// ===========================================
// dSP definitions
// ===========================================
//
// FFT setup
#define N_WAVE      2048 //1024
#define LOG2_N_WAVE 11   //10     /* log2(N_WAVE) 0 */

s1x14 Sinewave[N_WAVE];       // a table of sines for the FFT
s1x14 window[N_WAVE];         // a table of window values for the FFT
s1x14 fr[N_WAVE], fi[N_WAVE]; // input data
// dds output to FFT
s1x14 dds[N_WAVE]; // the dds output from ISR
// index into dds buffer
int buffer_index;
// displa points for graphiics
short display_data[N_WAVE];
short fft_display_data[N_WAVE];
short log_display_data[N_WAVE];

// ==================================
// === Init FFT arrays
//====================================
void FFTinit(void)
{
  // one cycle sine table
  //  required for FFT
  for (int ii = 0; ii < N_WAVE; ii++)
  {
    // one cycle per window for FFT -- scall amp for number of bits
    Sinewave[ii] = float_to_s1x14(0.5 * sin(6.283 * ((float)ii) / N_WAVE));
    // Raised cos window
    // I think the spectrgraph looks better with flat window
    window[ii] =
        float_to_s1x14(1.0); //- cos(6.283 * ((float) ii) / (N_WAVE - 1)));
  }
}

// ==================================
// === FFT
//====================================
void FFTfix(s1x14 fr[], s1x14 fi[], int m)
{
  //Adapted from code by:
  //Tom Roberts 11/8/89 and Malcolm Slaney 12/15/94 malcolm@interval.com
  //fr[n],fi[n] are real,imaginary arrays, INPUT AND RESULT.
  //size of data = 2**m
  // This routine does foward transform only

  int   mr, nn, i, j, L, k, istep, n;
  s1x14 qr, qi, tr, ti, wr, wi;

  mr = 0;
  n  = 1 << m; //number of points
  nn = n - 1;

  /* decimation in time - re-order data */
  for (m = 1; m <= nn; ++m)
  {
    L = n;
    do
      L >>= 1;
    while (mr + L > nn);
    mr = (mr & (L - 1)) + L;
    if (mr <= m)
      continue;
    tr     = fr[m];
    fr[m]  = fr[mr];
    fr[mr] = tr;
    ti     = fi[m];  //for real inputs, don't need this
    fi[m]  = fi[mr]; //for real inputs, don't need this
    fi[mr] = ti;     //for real inputs, don't need this
  }

  L = 1;
  k = LOG2_N_WAVE - 1;
  while (L < n)
  {
    istep = L << 1;
    for (m = 0; m < L; ++m)
    {
      j  = m << k;
      wr = Sinewave[j + N_WAVE / 4];
      wi = -Sinewave[j];
      //wr >>= 1; //do need if scale table
      //wi >>= 1;

      for (i = m; i < n; i += istep)
      {
        j     = i + L;
        tr    = muls1x14(wr, fr[j]) - muls1x14(wi, fi[j]);
        ti    = muls1x14(wr, fi[j]) + muls1x14(wi, fr[j]);
        qr    = fr[i] >> 1;
        qi    = fi[i] >> 1;
        fr[j] = qr - tr;
        fi[j] = qi - ti;
        fr[i] = qr + tr;
        fi[i] = qi + ti;
      }
    }
    --k;
    L = istep;
  }
}

//====================================
// === magnitude approx good to about +/-2%
// see https://en.wikipedia.org/wiki/Alpha_max_plus_beta_min_algorithm
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))
//====================================
void magnitude(s1x14 fr[], s1x14 fi[], int length)
{
  s1x14 mmax, mmin;
  s1x14 c1 = float_to_s1x14(0.89820);
  s1x14 c2 = float_to_s1x14(0.48597);
  for (int ii = 0; ii < length; ii++)
  {
    mmin   = min(abs(fr[ii]), abs(fi[ii])); //>>9
    mmax   = max(abs(fr[ii]), abs(fi[ii]));
    // reuse fr to hold magnitude
    fr[ii] = max(mmax, (muls1x14(mmax, c1) + muls1x14(mmin, c2)));
    fi[ii] = 0;
  }
}

// ==================================
// === approx log2 for plotting
//====================================
#define log_min 0x00
// reuse fr to hold log magnitude
// shifting finds most significant bit
// then make approxlog  = ly + (fr-y)./(y) + 0.043;
// BUT for an 8-bit approx (4 bit ly and 4-bit fraction)
// ly 1<=ly<=14
// omit the 0.043 because it is too small for 4-bit fraction
void log2_approx(s1x14 fr[], int length)
{
  //
  int sx, y, ly, temp;
  for (int i = 0; i < length; i++)
  {
    // interpret bits as integer
    sx = fr[i];
    y  = 1;
    ly = 0;
    while (sx > 1)
    {
      y  = y * 2;
      ly = ly + 1;
      sx = sx >> 1;
    }
    // shift ly into upper 4-bits as integer part of log
    // take bits below y and shift into lower 4-bits
    // !!NOTE that fr is no longer in s1x14 format!!
    fr[i] = ((ly) << 4) + ((fr[i] - y) >> (ly - 4));
    // bound the noise at low amp
    if (fr[i] < log_min)
      fr[i] = log_min;
  }
}

// ==================================================
// === graphics demo -- RUNNING on core 0
// ==================================================
static PT_THREAD(protothread_graphics(struct pt* pt))
{
  PT_BEGIN(pt);
  // update count -- for tuning cursor speed
  static int update_count;
  // the protothreads interval timer
  PT_INTERVAL_INIT();

  // draw a big, clunky banner
#define banner_bottom 50
  // Draw some filled rectangles at top of screen
  fillRect(64, 0, 176, banner_bottom, BLUE);    // blue box
  fillRect(250, 0, 176, banner_bottom, YELLOW); // red box
  fillRect(435, 0, 176, banner_bottom, GREEN);  // green box

  // Write some intro banner text
  setTextColor(WHITE);
  setCursor(65, 0);
  setTextSize(1);
  writeString("Raspberry Pi Pico");
  setCursor(65, 10);
  writeString("Graphics joystick demo");
  setCursor(65, 20);
  writeString("Hunter Adams & Bruce Land");
  setCursor(65, 30);
  writeString("vha3@cornell.edu");
  setCursor(65, 40);
  writeString("brl4@cornell.edu");

  setTextColor(BLACK);
  setCursor(290, 6);
  setTextSize(2);
  writeString("ECE 4760");

  setCursor(445, 10);
  setTextSize(1);
  writeString("Protothreads rp2040 v1.1.1");
  setCursor(445, 20);
  writeString("VGA - 640x480 - 16 colors ");
  setTextColor(WHITE);

  // draw a menu outline
#define menu_side   100
#define item_height 25
  // left edge
  drawLine(0, banner_bottom + 22, 0, 472, RED);
  // right edge
  drawLine(menu_side, banner_bottom + 22, menu_side, 472, RED);
  // item dividers
  for (short i = banner_bottom + 22; i < 480; i += item_height)
  {
    drawLine(0, i, menu_side, i, RED);
  }

  // test menu setup
  // bulky, but unavoidable
  menu[0].item_color  = WHITE;
  menu[1].item_color  = WHITE;
  menu[2].item_color  = WHITE;
  menu[3].item_color  = WHITE;
  menu[4].item_color  = GREEN;
  menu[5].item_color  = GREEN;
  menu[6].item_color  = GREEN;
  menu[7].item_color  = GREEN;
  menu[8].item_color  = GREEN;
  menu[9].item_color  = LIGHT_BLUE;
  menu[10].item_color = RED;
  //
  sprintf(menu[0].item_name, "Octave # ");
  sprintf(menu[1].item_name, "Attack main ");
  sprintf(menu[2].item_name, "Sustain main ");
  sprintf(menu[3].item_name, "Decay main ");
  sprintf(menu[4].item_name, "Fmod/Fmain ");
  sprintf(menu[5].item_name, "FM depth max ");
  sprintf(menu[6].item_name, "Attack FM ");
  sprintf(menu[7].item_name, "Sustain FM ");
  sprintf(menu[8].item_name, "Decay FM ");
  sprintf(menu[9].item_name, "Lin=1/Quad DK ");
  sprintf(menu[10].item_name, "Run ");
  //
  menu[0].item_data_type  = 0; // int
  menu[1].item_data_type  = 1; // float
  menu[2].item_data_type  = 1; // float
  menu[3].item_data_type  = 1; // float
  menu[4].item_data_type  = 1; // float
  menu[5].item_data_type  = 1; // float
  menu[6].item_data_type  = 1; // float
  menu[7].item_data_type  = 1; // float
  menu[8].item_data_type  = 1; // float
  menu[9].item_data_type  = 0; // int
  menu[10].item_data_type = 0; // int
  //
  menu[0].item_inc_type   = 0; // lin update
  menu[1].item_inc_type   = 1; // log update
  menu[2].item_inc_type   = 1; // log
  menu[3].item_inc_type   = 1; // log
  menu[4].item_inc_type   = 0; // lin update
  menu[5].item_inc_type   = 1; // log update
  menu[6].item_inc_type   = 1; // log
  menu[7].item_inc_type   = 1; // log
  menu[8].item_inc_type   = 1; // log
  menu[9].item_inc_type   = 0; //
  menu[10].item_inc_type  = 0; //
  //
  menu[0].item_increment  = 1;    //"Octave # ") ;
  menu[1].item_increment  = 0.99; //"Attack main ") ;
  menu[2].item_increment  = 1.1;  //"Sustain main ") ;
  menu[3].item_increment  = 0.98; //"Decay main ") ;
  menu[4].item_increment  = 0.01; //"Fmod/Fmain ") ;
  menu[5].item_increment  = 3.0;  //"FM depth max ") ;
  menu[6].item_increment  = 0.95; //"Attack FM ") ;
  menu[7].item_increment  = 1.1;  //"Sustain FM ") ;
  menu[8].item_increment  = 0.97; //"Decay FM ") ;
  menu[9].item_increment  = 1;    //"Lin=1/Quad DK ") ;
  menu[10].item_increment = 1;    // "Run ") ;

  // original
  menu[0].item_float_value  = 3;   // "Octave # ") ;
  menu[1].item_float_value  = .01; // "Attack main ") ;
  menu[2].item_float_value  = .3;  // "Sustain main ") ;
  menu[3].item_float_value  = .5;  // "Decay main ") ;
  menu[4].item_float_value  = 3;   // "Fmod/Fmain ") ;
  menu[5].item_float_value  = .25; // "FM depth max ") ;
  menu[6].item_float_value  = .01; // "Attack FM ") ;
  menu[7].item_float_value  = .1;  // "Sustain FM ") ;
  menu[8].item_float_value  = .4;  // "Decay FM ") ;
  menu[9].item_float_value  = 0;   // "Lin=1/Quad DK ") ;
  menu[10].item_float_value = 1;   //  "Run ") ;

  // drum
  // menu[0].item_float_value = 1 ;   //"Octave # ") ;
  // menu[1].item_float_value = 0.001 ;  //"Attack main ") ;
  // menu[2].item_float_value = 0 ;  //"Sustain main ") ;
  // menu[3].item_float_value = 0.99 ;   //"Decay main ") ;
  // menu[4].item_float_value = 1.6 ;   //"Fmod/Fmain ") ;
  // menu[5].item_float_value = 1.5 ;  //"FM depth max ") ;
  // menu[6].item_float_value = 0.001 ;  //"Attack FM ") ;
  // menu[7].item_float_value = 0 ;   //"Sustain FM ") ;
  // menu[8].item_float_value = 0.90 ;   //"Decay FM ") ;
  // menu[9].item_float_value = 1 ;  //"Lin=1/Quad DK ") ;
  // menu[10].item_float_value = 1 ;  // "Run ") ;

  //bowed string
  // menu[0].item_float_value = 3 ;   //"Octave # ") ;
  // menu[1].item_float_value = 0.99 ;  //"Attack main ") ;
  // menu[2].item_float_value = 1.0 ;  //"Sustain main ") ;
  // menu[3].item_float_value = 0.98 ;   //"Decay main ") ;
  // menu[4].item_float_value = 3.0 ;   //"Fmod/Fmain ") ;
  // menu[5].item_float_value = 3.0 ;  //"FM depth max ") ;
  // menu[6].item_float_value = 0.95 ;  //"Attack FM ") ;
  // menu[7].item_float_value = 1.0 ;   //"Sustain FM ") ;
  // menu[8].item_float_value = 0.97 ;   //"Decay FM ") ;
  // menu[9].item_float_value = 0 ;  //"Lin=1/Quad DK ") ;
  // menu[10].item_float_value = 1 ;  // "Run ") ;

  menu[0].item_float_min  = 1;
  menu[0].item_float_max  = 6;
  menu[1].item_float_min  = .001;
  menu[1].item_float_max  = 5;
  menu[2].item_float_min  = .001;
  menu[2].item_float_max  = 5;
  menu[3].item_float_min  = .001;
  menu[3].item_float_max  = 5;
  menu[4].item_float_min  = .001;
  menu[4].item_float_max  = 100;
  menu[5].item_float_min  = .001;
  menu[5].item_float_max  = 100;
  menu[6].item_float_min  = .001;
  menu[6].item_float_max  = 5;
  menu[7].item_float_min  = .001;
  menu[7].item_float_max  = 5;
  menu[8].item_float_min  = .001;
  menu[8].item_float_max  = 5;
  menu[9].item_float_min  = 0;
  menu[9].item_float_max  = 1;
  menu[10].item_float_min = 0;
  menu[10].item_float_max = 1;
  //

  draw_menu(3, banner_bottom + 26, menu_length);

  // init the ADC
  ADC_setup();

  // set up gpio 4 for joystick button
#define joystick_button 4
  gpio_init(joystick_button);
  gpio_set_dir(joystick_button, GPIO_IN);
  // pullup ON, pulldown OFF
  gpio_set_pulls(joystick_button, true, false);

  // init drawing
  draw_x      = menu_side >> 1;
  draw_y      = 100;
  last_draw_x = draw_x;
  last_draw_y = draw_y;

  while (true)
  {
    // count passes thru loop
    update_count++;
    // read x, y potentiometers
    // adc_select_input(0);
    // adc_x = adc_read();
    // adc_select_input(1);
    // adc_y = adc_read();
    // read the joystick pushbutton
    last_button = button;
    button      = !gpio_get(joystick_button);

    // derive a 'click' for one pass through the following code
    if (click == false && button == true && last_button == false)
    {
      click           = true;
      last_click_time = click_time;
      click_time      = PT_GET_TIME_usec();
    }
    else
      click = false;

      // get double click for one pass thru code
#define max_double_time 500000
    if (click == true && (click_time - last_click_time) < max_double_time)
    {
      double_click = true;
      click        = false;
    }
    else
      double_click = false;
    // testing
    //printf("%d %d %d %d\n\r", adc_x, adc_y, click, double_click);

    // save the old position for cursor erase
    last_draw_x = draw_x;
    last_draw_y = draw_y;

    // get the new posiion
    // joystick dead zone and about 4 step sizes +/-
    draw_x += (adc_x - 2048) / 512;
    // bounds check
    if (draw_x > 639)
      draw_x = 639;
    // check if a menu item has been clicked and keep cursor in the item
    //if  ((draw_x >= menu_side) && click_latch) draw_x=menu_side-1 ;
    if ((draw_x >= menu_side))
      draw_x = menu_side - 1;
    if (draw_x < 0)
      draw_x = 0;
    // y
    draw_y += (adc_y - 2048) / 512;
    if (draw_y > 639)
      draw_y = 639;
    if (draw_y < 0)
      draw_y = 0;

    // erase cursor BEFORE the draw stuff
    erase_cursor(last_draw_x, last_draw_y);

    // handle double click
    // menu item with serial input
    if (double_click == true && draw_y > banner_bottom)
    {
      // get item index
      clicked_item = (draw_y - banner_bottom - 22) / 25;
      // modify item change indicator
      fillCircle(95, banner_bottom + 26 + clicked_item * 25, 2, YELLOW);
      // serial prompt and get serial value
      // BLOCKS until <ENTER>!
      // print prompt
      sprintf(pt_serial_out_buffer, "New value: ");
      // spawn a thread to do the non-blocking write
      serial_write;
      // spawn a thread to do the non-blocking serial read
      serial_read;
      // convert input string to number
      sscanf(pt_serial_in_buffer, "%f ", &serial_value);
      //
      change_value_serial(clicked_item, serial_value);
      // update the menu text
      draw_item(3, banner_bottom + 26, clicked_item, menu_length);
      // cancel spurious click
      click_latch = false;
    }
    else
    {
      fillCircle(95, banner_bottom + 26 + clicked_item * 25, 2, BLACK);
    }

    // detect click in menu region and get item number
    if (click == true && draw_y > banner_bottom && draw_x < menu_side)
    {
      //if (click==true && ((draw_y>banner_bottom && draw_x<menu_side) | x_latch==draw_y)){
      // get item index and set update flag
      clicked_item = (draw_y - banner_bottom - 22) / 25;
      click_latch  = (click_latch) ? false : true;
      // save y value to make sure we are insame item later
      x_latch      = draw_y;
    }

    // change the clicked item
    //if(click_latch==true  && draw_y>banner_bottom && draw_x<menu_side){
    if (click_latch == true && draw_y == x_latch)
    {
      // slow down the inc/dec by 4x
      if ((update_count & 0x03) == 0)
        change_value(clicked_item, (adc_x - 2048) / 512);
      // update the menu text
      draw_item(3, banner_bottom + 26, clicked_item, menu_length);
      // item change indicator
      fillCircle(95, banner_bottom + 26 + clicked_item * 25, 2, RED);
    }
    else
    {
      fillCircle(95, banner_bottom + 26 + clicked_item * 25, 2, BLACK);
      click_latch = false;
    }

    // redraw cursor
    draw_cursor(draw_x, draw_y);

    // A brief nap about 16/ec
    PT_YIELD_usec(60000);
  }
  PT_END(pt);
} // graphics thread

// ==================================================
// === toggle25 thread on core 0
// ==================================================
// the on-board LED blinks
// just a heartbeat to make sure we did not crash
static PT_THREAD(protothread_toggle25(struct pt* pt))
{
  PT_BEGIN(pt);
  static bool LED_state = false;

  // set up LED p25 to blink
  gpio_init(25);
  gpio_set_dir(25, GPIO_OUT);
  gpio_put(25, true);
  // data structure for interval timer
  PT_INTERVAL_INIT();

  while (1)
  {
    // yield time 0.1 second
    //PT_YIELD_usec(100000) ;
    PT_YIELD_INTERVAL(100000);

    // toggle the LED on PICO
    LED_state = LED_state ? false : true;
    gpio_put(25, LED_state);
    //
    // NEVER exit while
  } // END WHILE(1)
  PT_END(pt);
} // blink thread

static PT_THREAD(protothread_readmux(struct pt* pt))
{
  PT_BEGIN(pt);
  // static bool LED_state = false;

  // // set up LED p25 to blink
  // gpio_init(25);
  // gpio_set_dir(25, GPIO_OUT);
  // gpio_put(25, true);
  // // data structure for interval timer
  // PT_INTERVAL_INIT();

  while (1)
  {

    // yield time 0.1 second
    PT_YIELD_usec(10000);
    // PT_YIELD_INTERVAL(100000);
    uint16_t result;
    uint16_t old = result;

    result = adc_read();

    // PT_YIELD_UNTIL(pt, result != old);

    //COMMENT OUT TO TRY PRESSING THROUGH SERIAL INSTEAD
    voltage = result * conversion_factor;
    // printf("Raw value: 0x%03x, voltage: %f V, on key %d \n", result, voltage, mux_select);

    prev_pressed[mux_select] = pressed[mux_select];
    if (mux_select > 7)
    {
      voltage = 3;
    }
    if (voltage < VOLTAGE_CUTOFF)
    {
      pressed[mux_select] = true;
    }
    else
    {
      pressed[mux_select] = false;
    }
    if (pressed[mux_select] && !prev_pressed[mux_select])
    {
      play_note[mux_select] = true;
    }
    //COMMENT OUT TO TRY PRESSING THROUGH SERIAL INSTEAD
    mux_select++;
    mux_select = mux_select >= NUM_KEYS ? 0 : mux_select;

    gpio_put(MUX_SEL_A, mux_select >> 0 & 0b1);
    gpio_put(MUX_SEL_B, mux_select >> 1 & 0b1);
    gpio_put(MUX_SEL_C, mux_select >> 2 & 0b1);
    //
    // NEVER exit while

  } // blink thread
    // NEVER exit while

  PT_END(pt);
} // blink thread

// ==================================================
// === FM parameter setup core1
// ==================================================
//
static PT_THREAD(protothread_FM(struct pt* pt))
{
  PT_BEGIN(pt);

  // convert alarm period in uSEc to rate
  Fs = 1.0 / ((float)alarm_period * 1e-6);
  //
  while (1)
  {

    // == Fout and Fmod are in Hz
    // == fm_depth is 0 to 10 or so
    // == times are in seconds
    // wait for the run command
    PT_YIELD_UNTIL(pt, menu[10].item_float_value == 1);

    // conversion to intrnal units
    // increment = Fout/Fs * 2^32
    // octave number is based on a C3 to C4 table

    octave_num = menu[0].item_float_value;
    Fmod       = menu[4].item_float_value;
    float current_note;
    for (int i = 0; i < NUM_KEYS; i++)
    {
      current_note = notes[i % 8] * pow(2, octave_num - 3);
      main_inc[i]  = current_note * pow(2, 32) / Fs;
      mod_inc[i]   = Fmod * current_note * pow(2, 32) / Fs;
    }
    // fm modulation strength
    max_mod_depth = float_to_fix(menu[5].item_float_value * 100000);

    // convert main input times to sample number
    attack_time      = float_to_fix(menu[1].item_float_value * Fs);
    decay_time       = float_to_fix(menu[3].item_float_value * Fs);
    sustain_time     = float_to_fix(menu[2].item_float_value * Fs);
    // and now get increments
    attack_inc       = div(max_amp, attack_time);
    // linear and parabolic fit
    decay_inc        = div(max_amp, decay_time);
    recip_decay_time = div(onefix, decay_time);
    //quad_decay_inc = mul((decay_inc<<1), recip_decay_time);
    // this need to be floating point becuase of squareing the decay time
    //f_quad_decay_inc = (fix_to_float(decay_inc<<1)/fix_to_float(decay_time));

    // convert modulation input times to sample number
    mod_attack_time  = float_to_fix(menu[6].item_float_value * Fs);
    mod_decay_time   = float_to_fix(menu[8].item_float_value * Fs);
    mod_sustain_time = float_to_fix(menu[7].item_float_value * Fs);
    // and now get increments
    // precomputing increments means that only add/subtract is needed
    mod_attack_inc   = div(max_mod_depth, mod_attack_time);
    mod_decay_inc    = div(max_mod_depth, mod_decay_time);
    if (printParams)
    {
      printParams = false;
      printf("--------------------------------------------\n"
             "octave_num: %f\nFmod: %f\nattack_time: %f\ndecay_time: %f\n"
             "sustain_time: %f\nattack_inc: %f\ndecay_inc: "
             "%f\nmod_attack_time: %f\n"
             "mod_decay_time: %f\nmod_sustain_time: %f\nmod_depth: %f\n",
             (octave_num),
             (Fmod),
             menu[1].item_int_value,
             menu[3].item_int_value,
             menu[2].item_int_value,
             fix_to_float(attack_inc),
             fix_to_float(decay_inc),
             menu[6].item_int_value,
             menu[8].item_int_value,
             menu[7].item_int_value,
             fix_to_float(max_mod_depth));
    }
    // tell the synth ISR to go
    static int i, j;

    // uint16_t result;

    // result = adc_read();
    // printf("Raw value: 0x%03x, voltage: %f V, on key %d \n", result, result * conversion_factor, mux_select);

    //COMMENT OUT TO TRY PRESSING THROUGH SERIAL INSTEAD
    // voltage = result * conversion_factor;

    // prev_pressed[mux_select] = pressed[mux_select];

    // if (voltage < VOLTAGE_CUTOFF) {
    //     pressed[mux_select] = true;
    // }
    // else {
    //     pressed[mux_select] = false;
    // }
    // if (pressed[mux_select] && !prev_pressed[mux_select]) {
    //     play_note[mux_select] = true;
    // }
    //COMMENT OUT TO TRY PRESSING THROUGH SERIAL INSTEAD
    // mux_select++;
    // mux_select = mux_select >= NUM_KEYS ? 0 : mux_select;

    // gpio_put(MUX_SEL_A, mux_select >> 0 & 0b1);
    // gpio_put(MUX_SEL_B, mux_select >> 1 & 0b1);
    // gpio_put(MUX_SEL_C, mux_select >> 2 & 0b1);

    // mod_attack_inc = div(current_depth, mod_attack_time) ;
    // mod_decay_inc = div(current_depth, mod_decay_time) ;
    // printf("pressed [0] %d - delay[0] = %d\n", pressed[0]?1:0,  fix_to_int(add_delay[0]));
    // printf("pressed [1] %d - delay[1] = %d\n", pressed[1]?1:0,  fix_to_int(add_delay[1]));
    //printf("Pressed [%d,%d,%d,%d,%d,%d,%d,%d]\n",pressed[0],pressed[1],pressed[2],pressed[3],pressed[4],pressed[5],pressed[6],pressed[7]);

    for (i = 0; i < NUM_KEYS; i++)
    {

      if (play_note[i])
      {
        current_main_inc[i] = main_inc[i];
        current_mod_inc[i]  = mod_inc[i];
        note_start[i]       = true;
        // PT_YIELD_usec(10000);
        //PT_YIELD_UNTIL(pt, current_amp[i]<onefix);
        //PT_YIELD_usec(10000);
        play_note[i]        = false;
      }
    }
    //PT_YIELD_usec(100000) ;

    // NEVER exit while
  } // END WHILE(1)
  PT_END(pt);
} // timer thread

// ==================================================
// === ISR routine -- RUNNING on core 1
// ==================================================
//
void compute_sample(void)
{
  // ===
  for (int i = 0; i < NUM_KEYS; i++)
  {
    // start a burst on new data
    if (note_start[i])
    {
      // reset the start flag
      note_start[i]        = false;
      // init the amplitude
      current_amp[i]       = attack_inc;
      current_mod_depth[i] = mod_attack_inc;
      // reset envelope time
      note_time[i]         = 0;
      // phase lock the main frequency
      main_accum[i]        = 0;
      add_delay[i]         = 0;
    } // note start
    else if (pressed[i] && prev_pressed[i])
    {
      add_delay[i] += onefix;
    }
    // play the burst as long as the amplitude is positive
    // as it decays linearly
    if (current_amp[i] > 0)
    {

      // update dds modulation freq
      mod_accum[i] += current_mod_inc[i];
      mod_wave[i]  = sine_table[mod_accum[i] >> 24];
      // get the instataneous modulation amp
      // update modulation amplitude envelope
      // printf("mod attack time is %f\n", fix_to_float(mod_attack_time));
      if (note_time[i] <
          (mod_attack_time + mod_decay_time + mod_sustain_time + add_delay[i]))
      {
        current_mod_depth[i] =
            (note_time[i] <= mod_attack_time)
                ? current_mod_depth[i] + mod_attack_inc
            : (note_time[i] <=
               mod_attack_time + mod_sustain_time + add_delay[i])
                ? current_mod_depth[i]
                : current_mod_depth[i] - mod_decay_inc;
      }
      else
      {
        current_mod_depth[i] = 0;
      }

      // set dds main freq and FM modulate it
      main_accum[i] += current_main_inc[i] +
                       (unsigned int)mul(mod_wave[i], current_mod_depth[i]);
      // update main waveform
      main_wave[i] = sine_table[main_accum[i] >> 24];

      // get the instataneous amp
      // update amplitude envelope
      // linear EXCEPT for optional parabolic decay
      if (note_time[i] <
          (attack_time + decay_time + sustain_time + add_delay[i]))
      {
        if (note_time[i] <= attack_time)
          current_amp[i] += attack_inc;
        else if (note_time[i] > attack_time + sustain_time + add_delay[i])
        {
          if (linear_dk == 1)
          {
            current_amp[i] -= decay_inc;
          }
          else
          {
            current_amp[i] = current_amp[i] - (decay_inc << 1) +
                             div(mul((decay_inc << 1),
                                     (note_time[i] - attack_time -
                                      sustain_time - add_delay[i])),
                                 decay_time);
          }
        }
      }
      else
      {
        current_amp[i] = 0;
      }
      // amplitide modulate and shift to the correct range for PWM
      main_wave[i] = mul(main_wave[i], current_amp[i]);
      // write final result  to 8-bit PWM
      //pwm_set_chan_level(pwm_slice_num, pwm_chan_num, fix_to_int(main_wave) + 128) ;
      //pwm_set_chan_level(pwm_slice_num, pwm_chan_num, fix_to_int(current_amp)) ;

      // move time ahead
      note_time[i] += onefix;
    } // current amp > 0
    // else {
    //     // set PWM to neutral level
    //     DAC_data = (DAC_config_chan_A | ((2048) & 0xfff))  ;
    // }

    // save in buffer for FFT
    if (buffer_index < N_WAVE)
    {
      dds[buffer_index++] = dds_to_s1x14(main_wave[i]);
    }
  }

  fix sum_waves = int_to_fix(0);

  for (int i = 0; i < NUM_KEYS; i++)
  {
    sum_waves += main_wave[i];
  }

  fix final_wave = div(sum_waves, int_to_fix(NUM_KEYS));
  //printf("final wave = %d\n", fix_to_int(final_wave));

  DAC_data = (DAC_config_chan_A | ((fix_to_int(final_wave) + 2048) & 0xfff));

  // Write data to DAC
  spi_write16_blocking(SPI_PORT, &DAC_data, 1);

} // end ISR call

// ==================================================
// === fft thread -- RUNNING on core 1
// ==================================================
//
static PT_THREAD(protothread_fft(struct pt* pt))
{
  PT_BEGIN(pt);
  static short time_column;
  static short fr_disp;
  // green > yellow > red
  static short color_map[10] = {0, 1, 2, 3, 11, 10, 9, 8, 12, 15};
  static short thread_time;

  PT_INTERVAL_INIT();

  FFTinit();
  time_column = 110;
  while (1)
  {
    //
    //thread_time = PT_GET_TIME_usec() - thread_time ;
    //printf ("%d\r\n", thread_time/1000);
    PT_YIELD_UNTIL(pt, buffer_index == N_WAVE);
    //thread_time = PT_GET_TIME_usec() ;

    memcpy(fr, dds, N_WAVE * 2);
    // start loading next buffer in ISR
    buffer_index = 0;
    //
    for (int i = 0; i < N_WAVE / 2; i++)
    {
      fr[i] = dds_to_s1x14(fr[i]);
    }

    // compute FFT on ADC buffer and draw one column

    // do the windowing
    for (int i = 0; i < N_WAVE; i++)
    {
      fr[i] = muls1x14(fr[i], window[i]);
      fi[i] = 0;
    }

    // do the FFT
    FFTfix(fr, fi, LOG2_N_WAVE);
    //
    // compute power spectrum
    magnitude(fr, fi, N_WAVE);

    // plot single spectrum for testing

    for (int i = 2; i < N_WAVE / 4; i++)
    {
      // erase a point
      drawPixel(i * 2 + 110, fft_display_data[i], BLACK);
      fft_display_data[i] = -(short)(fr[i] >> 2) + 150;
      drawPixel(i * 2 + 110, fft_display_data[i], GREEN);
    }
    // find max of magnitude for freq estimate
    s1x14 max_mag       = 0;
    int   max_mag_index = 0;
    for (int i = 2; i < N_WAVE / 2; i++)
    {
      //
      if (fr[i] > max_mag)
      {
        max_mag       = fr[i];
        max_mag_index = i;
      }
    }

    // plot log-spectrum for testing
    // !!After log, the returned fr is no longer in s1x14 format!!
    // do the APPROXIMATE log in u4x4 format!
    log2_approx(fr, N_WAVE / 2);
    // plot
    for (int i = 2; i < N_WAVE / 4; i++)
    {
      // erase a point
      drawPixel(i * 2 + 110, log_display_data[i], BLACK);
      fr[i]               = max(fr[i], 64) - 64;
      log_display_data[i] = -(short)(fr[i] >> 1) + 250;
      drawPixel(i * 2 + 110, log_display_data[i], GREEN);
    }

    // plot a vertical slice for spectrogram
    // Spectrogram -- draw and move right
    // wrap the screen
    // at right edge of screen, reset to left edge
    time_column += 2;
    if (time_column == 636)
      time_column = 110;
    for (int i = 1; i < 400; i++)
    {
      // bound to 0 to 7
      fr_disp = color_map[min(9, max((fr[i] >> 2), 0))]; //4-1
      drawPixel(time_column, 480 - i, fr_disp);
      //drawVLine(time_column, 480-2*i, 2, fr_disp) ;
    }

    // NEVER exit while
  } // END WHILE(1)
  PT_END(pt);
} // FFT thread

// User input thread. User can change draw speed
static PT_THREAD(protothread_serial(struct pt* pt))
{
  PT_BEGIN(pt);
  static char  classifier;
  static int   test_in;
  static float float_in;
  printParams = false;
  while (1)
  {
    classifier = 0;
    test_in    = 0;
    float_in   = 0.0;
    static char user_input_string[40];

    sprintf(pt_serial_out_buffer, ": ");
    serial_write;
    // spawn a thread to do the non-blocking serial read
    serial_read;
    // convert input string to number

    // original
    // menu[0].item_float_value = 3 ;// "Octave # ") ;
    // menu[1].item_float_value = .01 ;// "Attack main ") ;
    // menu[2].item_float_value = .3 ;// "Sustain main ") ;
    // menu[3].item_float_value = .5 ;// "Decay main ") ;
    // menu[4].item_float_value = 3 ;// "Fmod/Fmain ") ;
    // menu[5].item_float_value = .25 ;// "FM depth max ") ;
    // menu[6].item_float_value = .01 ;// "Attack FM ") ;
    // menu[7].item_float_value = .1 ;// "Sustain FM ") ;
    // menu[8].item_float_value = .4 ;// "Decay FM ") ;
    // menu[9].item_float_value = 0 ;// "Lin=1/Quad DK ") ;
    // menu[10].item_float_value = 1 ;//  "Run ") ;

    // sscanf(pt_serial_in_buffer, "%d %f", &test_in, &float_in);
    if ((test_in > 8 || test_in < 1))
    {
      sscanf(pt_serial_in_buffer, "%s %f", &user_input_string, &float_in);

      printParams = true;
      if (!strcmp(user_input_string, "mainmod"))
      {
        change_value_serial(4, float_in);
      }
      else if (!strcmp(user_input_string, "mainatk"))
      {
        change_value_serial(1, float_in);
      }
      else if (!strcmp(user_input_string, "modatk"))
      {
        change_value_serial(6, float_in);
      }
      else if (!strcmp(user_input_string, "mainsus"))
      {
        change_value_serial(2, float_in);
      }
      else if (!strcmp(user_input_string, "modsus"))
      {
        change_value_serial(7, float_in);
      }
      else if (!strcmp(user_input_string, "maindk"))
      {
        change_value_serial(3, float_in);
      }
      else if (!strcmp(user_input_string, "moddk"))
      {
        change_value_serial(8, float_in);
      }
      else if (!strcmp(user_input_string, "octave"))
      {
        change_value_serial(0, float_in);
      }
      else if (!strcmp(user_input_string, "moddepth"))
      {
        change_value_serial(5, float_in);
      }
      else if (!strcmp(user_input_string, "scale"))
      {
        int tn;
        for (tn = 0; tn < NUM_KEYS; tn++)
        {
          sprintf(pt_serial_out_buffer, "playing note %d", tn);
          serial_write;
          current_main_inc[tn] = main_inc[tn];
          current_mod_inc[tn]  = mod_inc[tn];
          note_start[tn]       = true;
          PT_YIELD_usec(1000000);

          // PT_YIELD_UNTIL(pt, current_amp[i-1]<onefix);
          // PT_YIELD_usec(10000);
          // play_note[i] = false;
        }
      }

      else
      {
        sprintf(pt_serial_out_buffer,
                "Command '%s' could not be recognized\n\r>>>",
                user_input_string);
        serial_write;
      }
    }
    else
    {
      mux_select = test_in - 1;
      sprintf(pt_serial_out_buffer,
              "Setting key '%d' to %f volts\n\r>>>",
              test_in,
              float_in);
      serial_write;
      voltage = float_in;
    }
  }
  PT_END(pt);
}

// ========================================
// === core 1 main -- started in main below
// ========================================
void core1_main()
{

  // fire off interrupt
  alarm_in_us(alarm_period);

  //  === add threads  ====================
  // for core 1
  pt_add_thread(protothread_FM);
  pt_add_thread(protothread_fft);
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
int main()
{
  // set the clock
  //set_sys_clock_khz(250000, true); // 171us

  for (int i = 0; i < NUM_KEYS; i++)
  {
    current_amp[i] = float_to_fix(2000.0);
  }

  for (int i = 0; i < NUM_KEYS; i++)
  {
    note_start[i] = true;
    play_note[i]  = false; // no keys pressed initially
  }

  // dds table 10 bit values
  int i = 0;
  while (i < 256)
  {
    // sine table is in naural +1/-1 range
    sine_table[i] = float_to_fix(sin(2 * 3.1416 * i / 256));
    i++;
  }

  // start the serial i/o
  stdio_init_all();
  // announce the threader version on system reset
  printf("\n\rProtothreads RP2040 v1.11 two-core\n\r");

  // Initialize SPI channel (channel, baud rate set to 20MHz)
  // connected to spi DAC
  spi_init(SPI_PORT, 20000000);
  // Format (channel, data bits per transfer, polarity, phase, order)
  spi_set_format(SPI_PORT, 16, 0, 0, 0);

  // Map SPI signals to GPIO ports
  //gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI);

  gpio_init(MUX_SEL_A);
  gpio_init(MUX_SEL_B);
  gpio_init(MUX_SEL_C);

  gpio_set_dir(MUX_SEL_A, GPIO_OUT);
  gpio_set_dir(MUX_SEL_B, GPIO_OUT);
  gpio_set_dir(MUX_SEL_C, GPIO_OUT);

  gpio_put(MUX_SEL_A, 0);
  gpio_put(MUX_SEL_B, 0);
  gpio_put(MUX_SEL_C, 0);

  // Initialize the VGA screen
  initVGA();

  // start core 1 threads
  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // === config threads ========================
  // for core 0
  pt_add_thread(protothread_graphics);
  pt_add_thread(protothread_toggle25);
  pt_add_thread(protothread_readmux);
  //
  // === initalize the scheduler ===============
  pt_schedule_start;
  // NEVER exits
  // ===========================================
} // end main