
/**
 * Hunter Adams (vha3@cornell.edu)
 * 
 * This demonstration animates two balls bouncing about the screen.
 * Through a serial interface, the user can change the ball color.
 *
 * HARDWARE CONNECTIONS
 *  - GPIO 16 ---> VGA Hsync
 *  - GPIO 17 ---> VGA Vsync
 *  - GPIO 18 ---> 470 ohm resistor ---> VGA Green 
 *  - GPIO 19 ---> 330 ohm resistor ---> VGA Green
 *  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
 *  - GPIO 21 ---> 330 ohm resistor ---> VGA Red
 *  - RP2040 GND ---> VGA GND
 *
 * RESOURCES USED
 *  - PIO state machines 0, 1, and 2 on PIO instance 0
 *  - DMA channels (2, by claim mechanism)
 *  - 153.6 kBytes of RAM (for pixel color data)
 *
 */

// Include the VGA grahics library
#include "vga16_graphics.h"
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
// Include Pico libraries
#include "pico/stdlib.h"
#include "pico/divider.h"
#include "pico/multicore.h"
// Include hardware libraries
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/spi.h"
// Include protothreads
#include "pt_cornell_rp2040_v1_3.h"

// === the fixed point macros ========================================
typedef signed int fix15 ;
#define multfix15(a,b) ((fix15)((((signed long long)(a))*((signed long long)(b)))>>15))
#define float2fix15(a) ((fix15)((a)*32768.0)) // 2^15
#define fix2float15(a) ((float)(a)/32768.0)
#define absfix15(a) abs(a) 
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a,b) (fix15)(div_s64s64( (((signed long long)(a)) << 15), ((signed long long)(b))))

// Wall detection
#define hitBottom(b) (b>int2fix15(380))
#define hitTop(b) (b<int2fix15(5))
#define hitLeft(a) (a<int2fix15(0))
#define hitRight(a) (a>int2fix15(620))

//DMA DECLARATIONS
// Number of samples per period in sine table
#define sine_table_size 256

// Sine table
int raw_sin[sine_table_size] ;

// Table of values to be sent to DAC
unsigned short DAC_data[sine_table_size] ;

// Pointer to the address of the DAC data table
unsigned short * address_pointer_dac = &DAC_data[0] ;

// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000

//SPI configurations
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define SPI_PORT spi0

//BIN Parameters
#define NUM_BINS 10
#define BIN_WIDTH 62
#define Y_MIN 400      // Bottom Y of the histogram
#define Y_MAX 480      // Top Y of the histogram
int bin_counts[NUM_BINS] = {0};  // Store number of balls per bin
double total_balls = 0;             // Track total balls dropped

// Number of DMA transfers per event
const uint32_t transfer_count = sine_table_size ;

// uS per frame
#define FRAME_RATE 33000

// the color of the boid
char color = WHITE ;
fix15 peg_x;
fix15 peg_y;
fix15 peg_xx;
fix15 peg_yy;
// Boid on core 0
fix15 boid0_x ;
fix15 boid0_y ;
fix15 boid0_vx ;
fix15 boid0_vy ;

// Boid on core 1
fix15 boid1_x ;
fix15 boid1_y ;
fix15 boid1_vx ;
fix15 boid1_vy ;

static fix15 peg_radius = int2fix15(6); // Peg radius
static fix15 ball_radius = int2fix15(2); // Ball radius
static fix15 gravity = float2fix15(1); // Gravity
static int noballs=50;
static int time=100;
static int elapsed=150;
#define NUM_BALLS 50
#define no_of_rows 16
#define NUM_PEGS ((no_of_rows * (no_of_rows + 1)) / 2)
// For a triangular peg layout (Galton board), the total number of pegs is
// the sum of the first no_of_rows natural numbers.

typedef struct {
    fix15 x, y;
    fix15 vx, vy;
    int last_peg;  // used to check if the peg changed
} ball_t;

typedef struct {
    fix15 x, y;
    fix15 radius;
} peg_t;

ball_t balls[NUM_BALLS];
peg_t pegs[NUM_PEGS];

// Function to initialize balls at random positions near the top
void initBalls() {
    for (int i = 0; i < NUM_BALLS; i++) {
        balls[i].x = int2fix15(320); // random x between 10 and 570
        balls[i].y = int2fix15(30);  // start near the top
        balls[i].vx = float2fix15(((rand() & 0xFFFF) * (2 / 65536.0)) - 1.0); // small random horizontal velocity
        balls[i].vy = int2fix15(0);
        balls[i].last_peg = -1;
    }
}

// Function to initialize pegs in a triangular (Galton) pattern
void initPegs(int rows) {
    int peg_index = 0;
    int start_x = 320;
    int start_y = 65;
    int y_spacing = 19;
    int x_spacing = 38;
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j <= i; j++) {
            pegs[peg_index].x = int2fix15(start_x + j * x_spacing - (i * x_spacing) / 2);
            pegs[peg_index].y = int2fix15(start_y + i * y_spacing);
            pegs[peg_index].radius = peg_radius;  // reuse your existing peg radius
            peg_index++;
        }
    }
}
// Create a boid
void spawnBoid(fix15* x, fix15* y,fix15* vx, fix15* vy, int direction)
{
  // Start in center of screen
  *x = int2fix15(320) ;
  *y = int2fix15(240) ;
  // Choose left or right
  if (direction) *vx = int2fix15(3) ;
  else *vx = int2fix15(-3) ;
  // Moving down
  *vy = int2fix15(1) ;
}

// Draw the boundaries
void drawArena() {
  drawVLine(100, 100, 280, WHITE) ;
  drawVLine(540, 100, 280, WHITE) ;
  drawHLine(100, 100, 440, WHITE) ;
  drawHLine(100, 380, 440, WHITE) ;
}

// This function displays a given message at a specified position on the VGA screen.
void displayMessage()
{ 
  setTextColor(WHITE);             // White text on black background
  setTextSize(0.2);                // Set small text size
  setCursor(10, 10);               // Set starting position
  writeString("Galton Board");
  setCursor(10, 20);
  writeString("Number of Balls:");
  char buffer[12];
  sprintf(buffer, "%d", noballs);
  writeString(buffer);
  setCursor(10, 30);
  writeString("Total Number:");
  char buffer2[12];
  sprintf(buffer2, "%d",time );
  writeString(buffer2);
  setCursor(10, 40);
  writeString("Tunable Parameters:");
  char buffer3[12];
  sprintf(buffer3, "%d", elapsed);
  writeString(buffer3); // Print the message using the provided function
  setCursor(10, 50);
  writeString("Total Time:");
  char buffer4[12];
  sprintf(buffer4, "%d",time_reached );
  writeString(buffer4); // Print the message using the provided function
}

//Histogram update
void updateHistogram(fix15 boid_x) {
  // Calculate the bin index based on x position
  int bin_index = fix2int15(boid_x) / BIN_WIDTH;

  // Ensure bin index is within valid range (0 to 9)
  if (bin_index >= 0 && bin_index < NUM_BINS) {
      bin_counts[bin_index]++;  // Increment count for this bin
  }

  total_balls++;  // Increment total balls count
}

//Histogram bars normalized
void drawHistogram() {
  // If no balls have been dropped, we skip drawing the histogram
  if (total_balls == 0) return;

  // Calculate the total number of balls across all bins
  int total_bins = 0;
  for (int i = 0; i < NUM_BINS; i++) {
      total_bins += bin_counts[i];
  }

  // Calculate the total available height for the bars (difference between Y_MAX and Y_MIN)
  int total_height = Y_MAX - Y_MIN;

  fillRect(0, 480, 640, 105, BLACK);
  // Now, draw the updated histogram bars for each bin
  for (int i = 0; i < NUM_BINS; i++) {
      int bin_x = i * BIN_WIDTH;  // X position of the bin

      // Normalize the bin count and calculate the height
      int bin_height = (bin_counts[i] * total_height) / total_balls;
      // Y position for the bin (drawing from bottom, Y_MAX being the top-most)
      int bar_y = Y_MAX - bin_height;

      // Draw the histogram bin as a rectangle with blue color
      fillRect(bin_x, bar_y, BIN_WIDTH - 1, bin_height, BLUE);
  }
}

// ==================================================
// === users serial input thread
// ==================================================
static PT_THREAD (protothread_serial(struct pt *pt))
{
    PT_BEGIN(pt);
    // stores user input
    static int user_input ;
    // wait for 0.1 sec
    PT_YIELD_usec(1000000) ;
    // announce the threader version
    sprintf(pt_serial_out_buffer, "Protothreads RP2040 v1.0\n\r");
    // non-blocking write
    serial_write ;
      while(1) {
        // print prompt
        sprintf(pt_serial_out_buffer, "input a number in the range 1-15: ");
        // non-blocking write
        serial_write ;
        // spawn a thread to do the non-blocking serial read
        serial_read ;
        // convert input string to number
        sscanf(pt_serial_in_buffer,"%d", &user_input) ;
        // update boid color
        if ((user_input > 0) && (user_input < 16)) {
          color = (char)user_input ;
        }
      } // END WHILE(1)
  PT_END(pt);
} // timer thread


// Animation on core 0
static PT_THREAD (protothread_anim(struct pt *pt))
{
    // Mark beginning of thread
    PT_BEGIN(pt);

    // Variables for maintaining frame rate
    static int begin_time ;
    static int spare_time ;
    initPegs(no_of_rows);   
    // Spawn a boid
    //spawnBoid(&boid0_x, &boid0_y, &boid0_vx, &boid0_vy, 1);
    
    while(1) {
      // Measure time at start of thread
      begin_time = time_us_32() ;     
      
      displayMessage();
      // Redraw pegs
      for (int p = 0; p < NUM_PEGS; p++) {
        fillCircle(fix2int15(pegs[p].x), fix2int15(pegs[p].y), fix2int15(peg_radius), GREEN);
      }
      //drawArena() ;
      // delay in accordance with frame rate
      spare_time = FRAME_RATE - (time_us_32() - begin_time) ;
      // yield for necessary amount of time
      PT_YIELD_usec(spare_time) ;
     // NEVER exit while
    }
      // END WHILE(1)
  PT_END(pt);
} // animation thread


static PT_THREAD (protothread_anim1(struct pt *pt))
{
    PT_BEGIN(pt);
    int data_chan = dma_claim_unused_channel(true);;
    int ctrl_chan = dma_claim_unused_channel(true);;
    static int begin_time;
    static int spare_time;
    static fix15 intermediate_term;
    static float bounciness = 0.5; // energy loss factor
    // Use a margin for early collision detection (adjust as needed)
    static fix15 margin = int2fix15(2);
    initBalls();
    
    while(1) {
      begin_time = time_us_32();
       // Erase previous ball drawings
      for (int i = 0; i < NUM_BALLS; i++) {
        fillCircle(fix2int15(balls[i].x), fix2int15(balls[i].y), fix2int15(ball_radius), BLACK);
      }
      
      // Process each ball (we have NUM_BALLS = 10 here, but can be extended)
      for (int i = 0; i < NUM_BALLS; i++) {
          // Check collision with every peg
          for (int p = 0; p < NUM_PEGS; p++) {
              fix15 dx = balls[i].x - pegs[p].x;
              fix15 dy = balls[i].y - pegs[p].y;
              if (absfix15(dx) < (ball_radius + peg_radius+ margin) && absfix15(dy) < (ball_radius + peg_radius +margin)) {
                  float fdx = fix2float15(dx);
                  float fdy = fix2float15(dy);
                  float distance = sqrtf(fdx * fdx + fdy * fdy);
                  float normal_x = fdx / distance;
                  float normal_y = fdy / distance;
                  float ball_vx = fix2float15(balls[i].vx);
                  float ball_vy = fix2float15(balls[i].vy);
                  float dot = normal_x * ball_vx + normal_y * ball_vy;
                  fix15 intermediate_term = float2fix15(-2.0 * dot);
                      if(intermediate_term > 0) {
                          // Immediately reflect the velocity without penetration
                          balls[i].x = pegs[p].x + float2fix15(normal_x * (distance + 1.0));
                          balls[i].y = pegs[p].y + float2fix15(normal_y * (distance + 1.0));
                          balls[i].vx += float2fix15(normal_x * (-2.0 * dot));
                          balls[i].vy += float2fix15(normal_y * (-2.0 * dot));
                          if(p != balls[i].last_peg) {
                              // Optionally trigger a sound via DMA here
                              balls[i].last_peg = p;
                              balls[i].vx = multfix15(balls[i].vx, float2fix15(bounciness));
                              balls[i].vy = multfix15(balls[i].vy, float2fix15(bounciness));
                          }
                      
                  }
              }
          }
          // Apply gravity and update position
          balls[i].vy = balls[i].vy + gravity;
          balls[i].x = balls[i].x + balls[i].vx;
          balls[i].y = balls[i].y + balls[i].vy;
          // Check boundaries and bounce
          if (hitLeft(balls[i].x) || hitRight(balls[i].x)) {
              balls[i].vx = -balls[i].vx;
          }
          if (hitTop(balls[i].y)) {
              balls[i].vy = -balls[i].vy;
          }
          // If the ball hits the bottom, update histogram and reset position
          if (hitBottom(balls[i].y)) {
              updateHistogram(balls[i].x);
              drawHistogram();
              balls[i].x = int2fix15(320);
              balls[i].y = int2fix15(20);
              balls[i].vx = float2fix15(((rand() & 0xFFFF) * (1.5 / 65536.0)) - 1.0);
              balls[i].vy = int2fix15(0);
              balls[i].last_peg = -1;
          }
          // Draw the ball in its updated position
          fillCircle(fix2int15(balls[i].x), fix2int15(balls[i].y), fix2int15(ball_radius), WHITE);
          
        }
        spare_time = FRAME_RATE - (time_us_32() - begin_time);
        PT_YIELD_usec(spare_time); 
  }
  PT_END(pt);
}
// ========================================
// === core 1 main -- started in main below
// ========================================
void core1_main(){
  // Add animation thread
  pt_add_thread(protothread_anim1);
  // Start the scheduler
  pt_schedule_start ;

}

// ========================================
// === main
// ========================================
// USE ONLY C-sdk library
int main(){
  // initialize stio
  stdio_init_all() ;

  // initialize VGA
  initVGA() ;
  
  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(SPI_PORT, 20000000) ;

  // Format SPI channel (channel, data bits per transfer, polarity, phase, order)
  spi_set_format(SPI_PORT, 16, 0, 0, 0);

  // Map SPI signals to GPIO ports, acts like framed SPI with this CS mapping
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

  // Build sine table and DAC data table
  int i ;
  for (i=0; i<(sine_table_size); i++){
      raw_sin[i] = (int)(2047 * sin((float)i*6.283/(float)sine_table_size) + 2047); //12 bit
      DAC_data[i] = DAC_config_chan_A | (raw_sin[i] & 0x0fff) ;
  }

  // Select DMA channels
  int data_chan = dma_claim_unused_channel(true);;
  int ctrl_chan = dma_claim_unused_channel(true);;

  // Setup the control channel
  dma_channel_config c = dma_channel_get_default_config(ctrl_chan);   // default configs
  channel_config_set_transfer_data_size(&c, DMA_SIZE_32);             // 32-bit txfers
  channel_config_set_read_increment(&c, false);                       // no read incrementing
  channel_config_set_write_increment(&c, false);                      // no write incrementing
  channel_config_set_chain_to(&c, data_chan);                         // chain to data channel

  dma_channel_configure(
      ctrl_chan,                          // Channel to be configured
      &c,                                 // The configuration we just created
      &dma_hw->ch[data_chan].read_addr,   // Write address (data channel read address)
      &address_pointer_dac,                   // Read address (POINTER TO AN ADDRESS)
      1,                                  // Number of transfers
      false                               // Don't start immediately
  );

  // Setup the data channel
  dma_channel_config c2 = dma_channel_get_default_config(data_chan);  // Default configs
  channel_config_set_transfer_data_size(&c2, DMA_SIZE_16);            // 16-bit txfers
  channel_config_set_read_increment(&c2, true);                       // yes read incrementing
  channel_config_set_write_increment(&c2, false);                     // no write incrementing
  // (X/Y)*sys_clk, where X is the first 16 bytes and Y is the second
  // sys_clk is 125 MHz unless changed in code. Configured to ~44 kHz
  dma_timer_set_fraction(0, 0x0017, 0xffff) ;
  // 0x3b means timer0 (see SDK manual)
  channel_config_set_dreq(&c2, 0x3b);                                 // DREQ paced by timer 0
  // chain to the controller DMA channel
  channel_config_set_chain_to(&c2, ctrl_chan);                        // Chain to control channel


  dma_channel_configure(
      data_chan,                  // Channel to be configured
      &c2,                        // The configuration we just created
      &spi_get_hw(SPI_PORT)->dr,  // write address (SPI data register)
      DAC_data,                   // The initial read address
      sine_table_size,            // Number of transfers
      false                       // Don't start immediately.
  );


  // start the control channel
  dma_start_channel_mask(1u << ctrl_chan) ;

  // start core 1 
  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // add threads
  pt_add_thread(protothread_serial);
  pt_add_thread(protothread_anim);

  // start scheduler
  pt_schedule_start ;
} 
