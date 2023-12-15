/**
 * Ignacio de Jesus Romo (ir93@cornell.edu)
 * Ryan Kolm (rak277@cornell.edu)
 * 
 * Developed from work by
 * Hunter Adams (vha3@cornell.edu)
 * 
 * Divided Mandelbrot set calculation and visualization
 * Uses PIO-assembly VGA driver.
 * 
 * Core 1 draws the bottom half of the set using fixed point.
 * Core 0 draws the top half of the set using fixed point, also.
 * Pixel data received from the I2C0 channel running the sister code 
 * is placed into the VGA screen buffer.  
 * 
 * https://vanhunteradams.com/FixedPoint/FixedPoint.html
 * https://vanhunteradams.com/Pico/VGA/VGA.html
 *
 * HARDWARE CONNECTIONS
 *  - GPIO 11 ---> I2C0 SDA
 *  - GPIO 12 ---> I2C0 SCL 
 *  - GPIO 16 ---> VGA Hsync
 *  - GPIO 17 ---> VGA Vsync
 *  - GPIO 18 ---> 330 ohm resistor ---> VGA Red
 *  - GPIO 19 ---> 330 ohm resistor ---> VGA Green
 *  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
 *  - RP2040 GND ---> VGA GND
 *  - RP2040 GND
 *  - RP2040 VBUS 
 *
 * RESOURCES USED
 *  - PIO state machines 0, 1, and 2 on PIO instance 0
 *  - DMA channels 0,1 (for VGA monitor) and 4 (for moving data from I2C0)
 *  - 153.6 kBytes of RAM (for pixel color data)
 *
 */
#include "vga_graphics.h"
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include <time.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////// Stuff for Mandelbrot ///////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////
// Fixed point data type
typedef signed int fix28 ;
#define multfix28(a,b) ((fix28)(((( signed long long)(a))*(( signed long long)(b)))>>28)) 
#define float2fix28(a) ((fix28)((a)*268435456.0f)) // 2^28
#define fix2float28(a) ((float)(a)/268435456.0f) 
#define int2fix28(a) ((a)<<28)
// the fixed point value 4
#define FOURfix28 0x40000000 
#define SIXTEENTHfix28 0x01000000
#define ONEfix28 0x10000000

// Maximum number of iterations
#define max_count 1000
#define I2C_CHAN i2c0
#define I2C_BAUD_RATE 1000000
#define SDA_PIN 8
#define SCL_PIN 9
#define PIXEL_MESSAGE_LENGTH
volatile int pico_id;
volatile int pico_count;
int end_x;
int start_x;
const int data_chan = 4;

void get_slice(){
    pico_id = 0;
    //At least one Pico available to compute (this one)
    pico_count = 1;
    
    time_t st = time(NULL);

    //For a second, assign out Pico IDs to Picos asking for one. 
    while(time(NULL) - st < 1){
        if (i2c0_hw->raw_intr_stat & 0b100000){
            i2c_write_raw_blocking(I2C_CHAN, &pico_count, 1);
            pico_count++;
            
            int throwaway = i2c0_hw->clr_rd_req;
        }
    }
    printf("\nPicos online: %i.", pico_count) ;

    //For each Pico on the line, send back the total count of Picos
    // available. 
    for (int i = 1; i < pico_count; i++){
        volatile uint64_t st = time_us_64();
        while(!(i2c0_hw->raw_intr_stat & 0b100000) ); 
        unsigned char out =  (pico_count);

        i2c_write_raw_blocking(I2C_CHAN, &out, 1 );
        int throwaway = i2c0_hw->clr_rd_req;
        //printf("\nWriting: %i", out);
        
        
    }

    end_x = (pico_id + 1) * (640/pico_count);
    start_x = end_x - 640/pico_count;
    
    printf("\nStarting. Slice: %i", pico_id);
}


////////////////////////////////////////////////////////////////////////////////////////////////////
// Core 1 entry point
void core1_entry() {
    // Mandelbrot values
    fix28 Zre, Zim, Cre, Cim ;
    fix28 Zre_sq, Zim_sq ;

    int i, j, count, total_count ;

    fix28 x[640] ;
    fix28 y[240] ;
    uint32_t begin_time ;
    uint32_t end_time ;
    uint32_t total_time ;

        // x values
        for (i=0; i<640; i++) {
            x[i] = float2fix28(-2.0f + 3.0f * (float)i/640.0f) ;
        }
        
        // y values
        for (j=0; j<240; j++) {
            y[j] = float2fix28( 1.0f - 2.0f * (float)(j+240)/480.0f) ;
        }

        total_count = 0 ;

        begin_time = time_us_32() ;

        for (i=pico_id; i<640; i+=pico_count) {
            for (j=239; j>-1; j--) {

                Zre = Zre_sq = Zim = Zim_sq = 0 ;

                Cre = x[i] ;
                Cim = y[j] ;

                count = 0 ;

                // Mandelbrot iteration
                while (count++ < max_count) {
                    Zim = (multfix28(Zre, Zim)<<1) + Cim ;
                    Zre = Zre_sq - Zim_sq + Cre ;
                    Zre_sq = multfix28(Zre, Zre) ;
                    Zim_sq = multfix28(Zim, Zim) ;

                    if ((Zre_sq + Zim_sq) >= FOURfix28) break ;
                }
                // Increment total count
                total_count += count ;

                // Draw the pixel
                if (count >= max_count) drawPixel(i, j+240, BLACK) ;
                else if (count >= (max_count>>1)) drawPixel(i, j+240, WHITE) ;
                else if (count >= (max_count>>2)) drawPixel(i, j+240, CYAN) ;
                else if (count >= (max_count>>3)) drawPixel(i, j+240, BLUE) ;
                else if (count >= (max_count>>4)) drawPixel(i, j+240, RED) ;
                else if (count >= (max_count>>5)) drawPixel(i, j+240, YELLOW) ;
                else if (count >= (max_count>>6)) drawPixel(i, j+240, MAGENTA) ;
                else drawPixel(i, j+240, RED) ;
            }
        }

        end_time = time_us_32() ;
        total_time = end_time - begin_time ;
        multicore_fifo_push_blocking(total_time) ;
    
}

unsigned char received[4]; //buffer for data. 
int rx_count[32]; //unused tally of transactions per core received. 
uint64_t time_since_last_rx_us;
long counter = 0; //count of total transactions, for evaluation purposes. 
void get_pixel(){
    time_since_last_rx_us = time_us_64();
    counter++;

    unsigned char color = received[0] & 0b111; 

    //Unpack pixel data. 
    int x = ((0b11111000 & received[0]) >> 3) + ((0b11111 & received[1]) << 5);
    int y = ((0b11100000 & received[1]) >> 5) + ((0b111111 & received[2]) << 3);

    drawPixel(x, y, color); //To VGA screen buffer. 
    //x_count[pico_id]++; //Increment received count for a particular core. For dead reckoning, left unused. 
    
    dma_channel_set_write_addr(data_chan, received , true); //Restart DMA channel 
    dma_hw->ints0 = 1u << data_chan; //Clear interrupt
}


int main() {

    // Initialize stdio
    stdio_init_all();

    // Initialize VGA
    initVGA() ;



    long rate = i2c_init(I2C_CHAN, I2C_BAUD_RATE);
    printf("I2C rate of: %li", rate);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);

    // Use external 2K resistors instead of pullups.
    // gpio_pull_up(SDA_PIN);
    // gpio_pull_up(SCL_PIN);

    //Hardware for i2c channel 0
    i2c_hw_t * i2c_hw = i2c_get_hw(i2c0);
    i2c_set_slave_mode(I2C_CHAN, true, 0x55);


    //Send to picos their slice. Wait about 250ms to get all writes from secondaries.
    //Blank received counts. 
    for (int i = 0; i < 32; i++){
        rx_count[i] = 0;
    }
    gpio_init(25);
    gpio_set_dir(25, true);
    gpio_put(25, false);


    //Select DMA channels. 
    //Channel 4: main channel to receive pixel data. 
    dma_channel_config dma_config = dma_channel_get_default_config(data_chan);
    channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_8); //one byte at a time
    channel_config_set_dreq(&dma_config, i2c_get_dreq(i2c0, false)); //receive data paced by tx dreq. 
    
    channel_config_set_read_increment(&dma_config, false); //i2c data in the same place
    channel_config_set_write_increment(&dma_config, true); //Move though buffer to place data. 
    channel_config_set_chain_to(&dma_config, data_chan); //Disables chaining. 
    dma_channel_set_write_addr(data_chan, received , false); //place things in "received"
    dma_channel_set_read_addr(data_chan, &(i2c_hw->data_cmd), false); //where i2c data comes from
    dma_channel_set_trans_count(data_chan, 3, false); //3 bytes per transaction
    dma_channel_set_irq0_enabled(data_chan, true); //enable interrupts
    dma_channel_configure(data_chan, &dma_config, received, &i2c_hw->data_cmd , 3, false);
   
    //Interrupt routine once data move is complete. 
    // Should move pixel data to screen buffer.
    irq_set_exclusive_handler(DMA_IRQ_0, get_pixel);
    irq_set_enabled(DMA_IRQ_0, true);

    //No harm in starting early, 
    // as math Picos don't issue writes until end of their get_slice

    dma_channel_start(data_chan);
    
    get_slice();
    gpio_put(25, true);

    uint32_t begin_time ;
    begin_time = time_us_32() ;

    // Launch core 1
    multicore_launch_core1(core1_entry) ;

    /////////////////////////////////////////////////////////////////////////////////////////////////////
    // ===================================== Mandelbrot =================================================
    /////////////////////////////////////////////////////////////////////////////////////////////////////
    // Mandelbrot values
    fix28 Zre, Zim, Cre, Cim ;
    fix28 Zre_sq, Zim_sq ;

    int i, j, count, total_count ;

    fix28 x[640] ;
    fix28 y[240] ;
   
    uint32_t end_time ;
    uint32_t core1_time ;
    float total_time_core_0 ;
    float total_time_core_1 ;

        // x values
        for (i=0; i<640; i++) {
            x[i] = float2fix28(-2.0f + 3.0f * (float)i/640.0f) ;
        }
        
        // y values
        for (j=0; j<240; j++) {
            y[j] = float2fix28( 1.0f - 2.0f * (float)j/480.0f) ;
        }

        total_count = 0 ;

        for (i=pico_id; i<640; i+=pico_count) {
            
            for (j=0; j<240; j++) {

                Zre = Zre_sq = Zim = Zim_sq = 0 ;

                Cre = x[i] ;
                Cim = y[j] ;

                count = 0 ;

                // Mandelbrot iteration
                while (count++ < max_count) {
                    Zim = (multfix28(Zre, Zim)<<1) + Cim ;
                    Zre = Zre_sq - Zim_sq + Cre ;
                    Zre_sq = multfix28(Zre, Zre) ;
                    Zim_sq = multfix28(Zim, Zim) ;

                    if ((Zre_sq + Zim_sq) >= FOURfix28) break ;
                }
                // Increment total count
                total_count += count ;

                // Draw the pixel
                if (count >= max_count) drawPixel(i, j, BLACK) ;
                else if (count >= (max_count>>1)) drawPixel(i, j, WHITE) ;
                else if (count >= (max_count>>2)) drawPixel(i, j, CYAN) ;
                else if (count >= (max_count>>3)) drawPixel(i, j, BLUE) ;
                else if (count >= (max_count>>4)) drawPixel(i, j, RED) ;
                else if (count >= (max_count>>5)) drawPixel(i, j, YELLOW) ;
                else if (count >= (max_count>>6)) drawPixel(i, j, MAGENTA) ;
                else drawPixel(i, j, RED) ;

            }
        }

        end_time = time_us_32() ;
        total_time_core_0 = (float)(end_time - begin_time)*(1./1000000.) ;

        core1_time = multicore_fifo_pop_blocking() ;
        total_time_core_1 = (float)(core1_time)*(1./1000000.) ;

        printf("\nTotal time core 0: %3.6f seconds \n", total_time_core_0) ;
        printf("\nTotal time core 1: %3.6f seconds \n", total_time_core_1) ;
        time_since_last_rx_us = max(time_since_last_rx_us, max(total_time_core_0, total_time_core_1));
        int st = time(NULL);

        //When done, maintain text showing some performance stats. 
        while (true){
            gpio_put(25, time(NULL) % 2);
            char buffer[32];
            itoa(pico_count * 2, buffer, 10);

            setCursor(0,10);
           
            setTextColor2(0, WHITE);
            setTextSize(2);
            
            writeString("Welcome to the Computron!\nActive Cores:");
            writeString(buffer);
            
            writeString("\nFinish time: ");
            if(time_us_64() - time_since_last_rx_us > 1000000){
                gcvt((time_since_last_rx_us - begin_time) *(1./1000000.) , 10, buffer);
               
                writeString(buffer);
            } 
            else{
                st = time(NULL);
            }

            itoa(counter, buffer, 10);
            writeString("\nTotal transactions: "); writeString(buffer);
    
        }
  
    
}
