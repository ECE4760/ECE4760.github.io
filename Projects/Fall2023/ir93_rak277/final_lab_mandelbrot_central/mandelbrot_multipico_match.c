/**
 * Ignacio de Jesus Romo (ir93@cornell.edu)
 * Ryan Kolm (rak277@cornell.edu)
 * 
 * Developed from work by
 * Hunter Adams (vha3@cornell.edu)
 * 
 * Partial Mandelbrot set calculation and visualization
 * 
 * Core 1 draws the bottom half of the set using fixed point.
 * Core 0 draws the top half of the set using fixed point, also.
 * Pixel data is send to the central projector Pico over I2C.
 *
 * HARDWARE CONNECTIONS
 *  - GPIO 11 ---> I2C0 SDA
 *  - GPIO 12 ---> IC20 SCL
 *  - RP2040 VBUS 
 *  - RP2040 GND 
 *
 * RESOURCES USED
 *  - I2C0 hardware.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"
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
#define TEST_VALUE -1
#define MAX_ATTEMPTS 10000000
enum colors {BLACK, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE};

unsigned char pico_id;
unsigned char pico_count;
volatile unsigned char color_id;
volatile unsigned char outgoing[5];
int data_chan = 0;
volatile int semaphore[2];
volatile int failedAttempts = 0;

//Send pixel over to draw. 
void drawPixel(int x, int y, char color, int core_id){

    outgoing[0] = (color & 0b111) + ((x & 0b11111) << 3);
    outgoing[1] = ((x & 0b1111100000) >> 5) + ((y & 0b111) << 5);
    outgoing[2] = (y & 0b111111000) >> 3;

    //Only send out if color isn't black. 
    if(color != BLACK){
        //Wait until bus is unoccupied.
        while(!i2c_get_write_available(i2c0)) {}

        //Issue a blocking I2C write to the projector.
        int result = i2c_write_blocking(i2c0, 0x55, outgoing, 3, false);
        //Debug print statement.
        //printf("\n:%i. X,Y: %i, %i", opico_id, ox, oy);

        //Try again if the transaction failed, up to a maximum amount.
        if (failedAttempts < MAX_ATTEMPTS){
            if (result == PICO_ERROR_GENERIC){
                failedAttempts++;
                drawPixel(x,y,color, core_id);
            }
            else if (result == PICO_ERROR_TIMEOUT){

                failedAttempts++;
                drawPixel(x,y,color, core_id);
            }
        }
        if (result == 3) failedAttempts = 0;
    }
    
}

//
int start_x;
int end_x;

void get_slice(){

    //buffer to hold I2C read result. 
    unsigned char placeholder[4] = {69, 70, 71, 72};
    int result = 0;
    printf("\nGetting slice...");

    //Repeatedly try to read Pico ID fron the projector. 
    do{
        printf("\nAttempting to get slice...");
        result = i2c_read_timeout_us(i2c0, 0x55, placeholder, 1, false, 1000);
        if(result != 0) printf("\n Error type: %i", result);
    }
    while(result == PICO_ERROR_GENERIC || result == PICO_ERROR_TIMEOUT);
    
    //Delay
    time_t st = time(NULL); 
    while(time(NULL) - st < 2);

    printf("\nGot %i. Getting total...", placeholder[0]);
    pico_id = placeholder[0];

    //Keep tring to read the total amount of Picos on the line. 
    int count_received = 0;
    do{
        count_received = i2c_read_timeout_us(i2c0, 0x55, placeholder, 1, false, 1000);
    }
    while (count_received == 0 || count_received == PICO_ERROR_TIMEOUT || count_received == PICO_ERROR_GENERIC);

    printf("\nInput: %i", placeholder[0]);

    //Get pico count. If zero, stall permanently because something is wrong. 
    pico_count = (placeholder[0] & 0b1111);
    if(pico_count == 0) while(1){}
    printf("\nGot: %i, %i", pico_id, pico_count);

    //Calculate starting and ending position. 
    end_x = (pico_id + 1) * (640/pico_count);
    start_x = end_x - 640/pico_count;

    printf("\nBeginning with slice: %i x %i", start_x, end_x);
    //Enable LED to indicate it's begun processing. 
    gpio_put(25, 1);
    uint64_t st_us = time_us_64();
    //Small wait to wait for Picos to get online before issuing a write. 
    while(time_us_64() - st_us < 10000){};
    return;
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

        //for (i=start_x; i<end_x; i++) {
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
                while(semaphore[0]);
                semaphore[1] = 1;
                failedAttempts = 0;
                if (count >= max_count) drawPixel(i, j+240, BLACK, 1) ;
                else if (count >= (max_count>>1)) drawPixel(i, j+240, WHITE, 1) ;
                else if (count >= (max_count>>2)) drawPixel(i, j+240, CYAN, 1) ;
                else if (count >= (max_count>>3)) drawPixel(i, j+240, BLUE, 1) ;
                else if (count >= (max_count>>4)) drawPixel(i, j+240, RED, 1) ;
                else if (count >= (max_count>>5)) drawPixel(i, j+240, YELLOW, 1) ;
                else if (count >= (max_count>>6)) drawPixel(i, j+240, MAGENTA, 1) ;
                else drawPixel(i, j+240, RED, 1) ;
                semaphore[1] = 0;
            }
        }

        end_time = time_us_32() ;
        total_time = end_time - begin_time ;
        multicore_fifo_push_blocking(total_time) ;
}



int main() {

    // Initialize stdio
    stdio_init_all();

    // Initialize I2C
    
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_init(25);
    gpio_set_dir(25, true);
    gpio_put(25, false);
    
    // Use external 2K resistors instead of the pullups!
    // gpio_pull_up(SDA_PIN);
    // gpio_pull_up(SCL_PIN);
    //Should get I2C going at its max supported rate of 1 Mbps.
    long rate = i2c_init(I2C_CHAN, I2C_BAUD_RATE);
    printf("I2C rate of: %li", rate);

    //Get assignment from projector Pico.
    get_slice();


    // Launch core 1
    //Initialize semaphores first, though. 
    semaphore[0] = 0;
    semaphore[1] = 0;
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
    uint32_t begin_time ;
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


        begin_time = time_us_32() ;

        //for (i=start_x; i<end_x; i++) {
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
                while(semaphore[1]);
                semaphore[0] = 1;
                failedAttempts++;
                if (count >= max_count) drawPixel(i, j, BLACK, 0) ;
                else if (count >= (max_count>>1)) drawPixel(i, j, WHITE, 0) ;
                else if (count >= (max_count>>2)) drawPixel(i, j, CYAN, 0) ;
                else if (count >= (max_count>>3)) drawPixel(i, j, BLUE, 0) ;
                else if (count >= (max_count>>4)) drawPixel(i, j, RED, 0) ;
                else if (count >= (max_count>>5)) drawPixel(i, j, YELLOW, 0) ;
                else if (count >= (max_count>>6)) drawPixel(i, j, MAGENTA, 0) ;
                else drawPixel(i, j, RED, 0) ;
                semaphore[0] = 0;
               

            }
        }

        end_time = time_us_32() ;
        total_time_core_0 = (float)(end_time - begin_time)*(1./1000000.) ;

        core1_time = multicore_fifo_pop_blocking() ;
        total_time_core_1 = (float)(core1_time)*(1./1000000.) ;

        printf("\nTotal time core 0: %3.6f seconds \n", total_time_core_0) ;
        printf("\nTotal time core 1: %3.6f seconds \n", total_time_core_1) ;
        // printf("Total iterations: %d", total_count) ;
        
        //When finished, just blink on and off. 
        while(true){
            gpio_put(25, time(NULL) % 2);
        }
}
