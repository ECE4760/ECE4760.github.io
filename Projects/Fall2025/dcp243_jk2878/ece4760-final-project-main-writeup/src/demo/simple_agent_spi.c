

/**
 *
 * HARDWARE CONNECTIONS
 *
 * SPI:
 * - RP2040 GND ---> VGA-GND
 * - GPIO 5 (pin 7) Chip select
 * - GPIO 6 (pin 9) SCK/spi0_sclk
 * - GPIO 7 (pin 10) MOSI/spi0_tx
 * - GPIO 2 (pin 4) GPIO output for timing ISR
 * - 3.3v (pin 36) -> VCC on DAC
 * - GND (pin 3)  -> GND on DAC
 *
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
#include "pico/float.h"
#include "pico/printf.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
//
#include "hardware/clocks.h"
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
// === COMMUNICATION
// ==========================================
#include "hardware/spi.h"


// Low-level alarm infrastructure we'll be using
#define ALARM_NUM 0
#define ALARM_IRQ TIMER_IRQ_0



/////////////////////////////// GRAPHICS ///////////////////////////////////////


#define FOOD_RADIUS 6
#define HEAD_RADIUS 6
#define TRAIL_THICKNESS 6


int32_t spare_time = 33000;


// Param/stat strings
static const char time_format[] = "Time elapsed: %d";


// Screen size parameters
const int bottom_side = 479;
const int top_side = 0;
const int height = bottom_side - top_side;
const int left_side = 0;
const int right_side = 639;
const int width = right_side - left_side;




///////////////////////////////// SPI CONFIG ////////////////////////////////////////


// SPI configurations
#define PIN_MISO 4
#define PIN_CS 5
#define PIN_SCK 6
#define PIN_MOSI 7
#define SPI_PORT spi0


///////////////////////////////// PHYSICS //////////////////////////////////////


static PT_THREAD(protothread_graphics(struct pt *pt)) {
  PT_BEGIN(pt);
  while (true)  {
    PT_YIELD_usec(spare_time);
  }
  PT_END(pt);
} // graphics thread










// ==================================================
// === toggle25 thread on core 0
// ==================================================
// the on-board LED blinks
static PT_THREAD(protothread_led(struct pt *pt)) {
  PT_BEGIN(pt);
  static bool LED_state = false;


  // set up LED p25 to blink
  gpio_init(25);
  gpio_set_dir(25, GPIO_OUT);
  gpio_put(25, true);
  // data structure for interval timer
  PT_INTERVAL_INIT();


  while (1) {
    // yield time 0.25 second
    PT_YIELD_usec(250000) ;
    if (spare_time > 0) {


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






uint16_t current_pos[2] = {0,0};


void movement_planner() {
    current_pos[0] += (rand() % 3) - 1;
    current_pos[1] += (rand() % 3) - 1;
}


//Assuming that the amount of food is constant
//Assuming that the amount of enemies is constant
#define MAX_FOOD 25
#define MAX_ENEMIES 3
// ball_t food[MAX_FOOD];
// ball_t * food_pointer = &food[0];
uint16_t food_x[MAX_FOOD];
uint16_t * food_x_pointer = &food_x[0];
uint16_t food_y[MAX_FOOD];
uint16_t * food_y_pointer = &food_y[0];


uint16_t snake_x[MAX_FOOD];
uint16_t * snake_x_pointer = &snake_x[0];
uint16_t snake_y[MAX_FOOD];
uint16_t * snake_y_pointer = &snake_y[0];




// ==================================================
// === handle data entry from the main server
// ==================================================
static PT_THREAD(protothread_spi(struct pt *pt)) {
  PT_BEGIN(pt);
  static int num_enemies = MAX_ENEMIES;
  static short snake_num = 0;
  //int *ptr = (int *)malloc(num_food);
  //int *food_x = int[100];
  //ball_t balls[MAX_BALLS];
  //unsigned char vga_data_array[TXCOUNT];
  //char * address_pointer = &vga_data_array[0] ;


  static uint16_t frame_header[3] = {0,0,0}; // sync word, frame id, alive mask
  static bool i_am_alive = true;
  while (!spi_is_readable(SPI_PORT)){ // wait to recieve information
        PT_YIELD(pt);
  }
 
  spi_read16_blocking(SPI_PORT, 0, &snake_num, 1); //Read in the number of the snake that this controller is controlling


  while (1) {
    PT_WAIT_UNTIL(pt, spi_is_readable(SPI_PORT));
     //stop the spi port from writing information in read mode


   


    spi_read16_blocking(SPI_PORT, 0, frame_header, 3); //Read in the frame header


    if (frame_header[0] != 0xA5A5) {
            // For now, just continue and ignore this frame.
            continue;
    }
    i_am_alive = (frame_header[2] & (1u << snake_num)) != 0;
    if (i_am_alive){
        spi_read16_blocking(SPI_PORT, 0, food_x, MAX_FOOD);
        spi_read16_blocking(SPI_PORT, 0, food_y, MAX_FOOD);


        spi_read16_blocking(SPI_PORT, 0, snake_x, num_enemies);
        spi_read16_blocking(SPI_PORT, 0, snake_y, num_enemies);


        current_pos[0] = snake_x[snake_num];
        current_pos[1] = snake_y[snake_num];
        movement_planner();


        PT_WAIT_UNTIL(pt, spi_is_readable(SPI_PORT));
        // turn back on the abiility to write information with spi port
        spi_write16_blocking(SPI_PORT, current_pos, 2);
    }


   
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
  pt_add_thread(protothread_spi);
  pt_add_thread(protothread_led);
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
  set_sys_clock_khz(150000, true);


  // SERIAL //


  // OTHER IO //


  // Initidalize stdio
  stdio_init_all();


  // SPI & DMA TO DAC //


  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(SPI_PORT, 20000000);


  // Format SPI channel (channel, data bits per transfer, polarity, phase,
  // order)
  spi_set_format(SPI_PORT, 16, 0, 0, 0);
  spi_set_slave(SPI_PORT, true);




  // Map SPI signals to GPIO ports, acts like framed SPI with this CS mapping
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);


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

