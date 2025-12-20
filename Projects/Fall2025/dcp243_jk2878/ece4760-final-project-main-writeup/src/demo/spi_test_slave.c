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


/////////////////////////////// GRAPHICS ///////////////////////////////////////

char out_msg_buffer[] = "ACK!\n";
char in_msg_buffer[255];

//////////////////////////////// SPI //////////////////////////////////////////


// ========================================
// === core 0 main
// ========================================
int main() {
  // set the clock
  set_sys_clock_khz(250000, true);

  // OTHER IO //

  // Initidalize stdio
  stdio_init_all();

  // SPI & DMA TO DAC //

  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(AG_SPI_PORT, SPI_BAUD);

  // Format SPI channel (channel, data bits per transfer, polarity, phase,
  // order)
  spi_set_format(AG_SPI_PORT, SPI_BITS, SPI_POL, SPI_PHASE, SPI_ORDER);

  // Set to master
  spi_set_slave(AG_SPI_PORT, true);

  // Map SPI signals to GPIO ports, acts like framed SPI with this CS mapping
  gpio_set_function(AG_SPI_MISO, GPIO_FUNC_SPI);
  gpio_set_function(AG_SPI_CS, GPIO_FUNC_SPI);
  gpio_set_function(AG_SPI_SCK, GPIO_FUNC_SPI);
  gpio_set_function(AG_SPI_MOSI, GPIO_FUNC_SPI);

  gpio_init(25);
  gpio_set_dir(25, GPIO_OUT);
  gpio_put(25, true);

  bool gpio_state = true;

  while(1) {
    while(!spi_is_readable(AG_SPI_PORT));
    spi_read_blocking(AG_SPI_PORT, 0xff, in_msg_buffer, sizeof(in_msg_buffer));
    gpio_state = !gpio_state;
    gpio_put(25, gpio_state);
    //memcpy(out_msg_buffer, in_msg_buffer, sizeof(in_msg_buffer));
    while(!spi_is_writable(AG_SPI_PORT));
    spi_write_blocking(AG_SPI_PORT, out_msg_buffer, sizeof(out_msg_buffer));
  }
} // end main
