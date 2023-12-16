 //Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <pico/multicore.h>
#include <pico/divider.h>

//Include Pico libraries
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/divider.h"


//Include hardware libraries
#include "hardware/pwm.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
//#include "hardware/interp.h"
#include "hardware/uart.h"
#include "hardware/sync.h"
#include "hardware/spi.h"

//Include protothreads
#include "pt_cornell_rp2040_v1.h"


// Include custom libraries
#include "vga_graphics.h"
#include "mpu6050.h"

//Direct Digital Synthesis (DDS) parameters
#define two32 4294967296.0  // 2^32 (a constant)
#define Fs 40000            // sample rate

// the DDS units - core 0
// Phase accumulator and phase increment. Increment sets output frequency.
volatile unsigned int phase_accum_main_0;
volatile unsigned int phase_incr_main_0 = (400.0*two32)/Fs ;

// DDS sine table (populated in main())
#define sine_table_size 256
fix15 sin_table[sine_table_size] ;

// Values output to DAC
int DAC_output_0 ;
int DAC_output_1 ;

// Amplitude modulation parameters and variables
fix15 max_amplitude = int2fix15(1) ;    // maximum amplitude
fix15 attack_inc ;                      // rate at which sound ramps up
fix15 decay_inc ;                       // rate at which sound ramps down
fix15 current_amplitude_0 = 0 ;         // current amplitude (modified in ISR)
fix15 current_amplitude_1 = 0 ;         // current amplitude (modified in ISR)

// Timing parameters for beeps (units of interrupts)
#define ATTACK_TIME             2000
#define DECAY_TIME              500
#define SUSTAIN_TIME            1000


//Timing interval for swoops
#define SWOOP_DURATION           5200
//Timing interval for chirps
#define CHIRP_DURATION           5720
//Timing interval for cont sound
#define CONT_DURATION           10000

// State machine variables
volatile unsigned int STATE_0 = 1;
volatile unsigned int count_0 = 0 ;

// SPI data
uint16_t DAC_data_1 ; // output value
uint16_t DAC_data_0 ; // output value

// DAC parameters (see the DAC datasheet)
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

//SPI configurations (note these represent GPIO number, NOT pin number)
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define LDAC     8
#define LED      25
#define SPI_PORT spi0

#define LED             25


char keytext[40];
int prev_key = 0;
int frequency_swoop;
int frequency_chirp;

// Variable to store core number
volatile int corenum_0  ;


// Global counter for spinlock experimenting
volatile int global_counter = 0 ;
static int p;

//ultrasonic sensor code for dist

#define trig_pin 15
#define echo_pin 14

//Ultra-sonic sensor variables
int timeout = 26100;
int pulse;
//dist from ultrasonic sensor
volatile float dist;
//prev_dist keeps track of previous location of cube
volatile float prev_dist=0;

//notes of theremin
float A_note;
float D_note;
float G_note;
float E_note;
//pure notes are A4, D4, G3, E5


//scale the distance so can use in cont sound mode
volatile float dist_scaled;

//store the cont sound signal
float cont_sound;

//amplitude for notes
volatile float amplitude = 1;

//button to change between cont and disrete sound
//button is 1; play discrete sounds (default setting)
//button 0; play cont sounds
#define BUTTON 11


////////////////////////Graphics Variables
float scale = 50;
float angle = 0;

//Points of cube vertices
float points[8][3] = {{-1, -1, 1}, {1, -1, 1}, {1,  1, 1}, {-1, 1, 1}, {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1} };

//Standard projection matrix
float proj[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 0}};

//Matrix to store projected 2d points
int proj_points [8][2] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}, {4, 4}, {5, 5}, {6, 6}, {7, 7}};

//Vectors to store intermediate results from matrix operations
float rotated_x[3][1]; //Result from mult_x function
float rotated_y[3][1]; //Result from mult_y function
float projected2d[3][1];
float reshaped_vector[3][1]; //Result for rehsape function


/////////////////////////////////////Theremin Helper Functions///////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////

//determines how long it takes for the 
//echo pin to read the trig signal
//from rising edge to rising edge
float getPulse(int trigPin, int echoPin)
{
    static float begin_time ;
    static float end_time ;
    

    static float width = 0;

    while (gpio_get(echoPin) == 0){
    } 
    begin_time = time_us_32() ; 
    while (gpio_get(echoPin) == 1) 
    {
        width++;
        sleep_us(1);
    }
    
    end_time = time_us_32() ; 
    return end_time - begin_time ;
}

//converts the time that it takes to read the trig pulse
//into a dist measurement in cm
float getCm(int trigPin, int echoPin)
{   gpio_put(10, 1) ;
    float pulseLength = getPulse(trigPin, echoPin);
    gpio_put(10, 0) ;
    return pulseLength*0.0172;
}

/////////////////////////////////////Graphics Helper Functions///////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////
//Draws a line from two specific vertices
void connect_points (int i, int j){
    drawLine((short)(proj_points[i][0]), ((short)proj_points[i][1]), (short)(proj_points[j][0]), (short)(proj_points[j][1]), WHITE);
    printf("connect %f, %f, %f, %f\n",points[i][0], points[i][1], points[j][0], points[j][1]);
}

//Reshape 1x3 vector to 3x1 column vector for matrix operations
void reshape(float vector[3]){
    for (int i = 0; i < 3; ++i) {
        reshaped_vector[i][0] = vector[i];
    }
}

//Matrix multiplication function to rotate about x
void mult_x(float matrix[3][3] , float b[3][1]) {
       for (int i = 0; i < 3; ++i) {
           rotated_x[i][0] = 0;
            for (int j = 0; j < 3; ++j) {
                rotated_x[i][0] += matrix[i][j] * b[j][0];
            }
       }
}

//Matrix multiplication function to rotate about y
void mult_y(float matrix[3][3] , float b[3][1]) {
       for (int i = 0; i < 3; ++i) {
           rotated_y[i][0] = 0;
            for (int j = 0; j < 3; ++j) {
                rotated_y[i][0] += matrix[i][j] * b[j][0];
            }
       }
}

//Matrix multiplication of projection matrix with rotated 3D point
void mult_project(float matrix[3][3] , float b[3][1]) {
       for (int i = 0; i < 3; ++i) {
           projected2d[i][0] = 0;
            for (int j = 0; j < 3; ++j) {
                projected2d[i][0] += matrix[i][j] * b[j][0];
            }
       }
}

void drawCube(){
    fillRect(0,0, 700, 700, BLACK);

        //Rotation matrices
        float rotation_z[3][3] = {{cos(angle), -sin(angle), 0}, {(sin(angle), cos(angle),0)}, {0,0,1}};
        float rotation_y[3][3] = {{cos(angle), 0, sin(angle)}, {0, 1, 0}, {-sin(angle), 0, cos(angle)}};
        float rotation_x[3][3] = {{1, 0, 0}, {0, cos(angle), -sin(angle)}, {0, sin(angle), cos(angle)}};
    
        //Gets all 8 cube vertices
        for(int i=0; i<8; i++){
            reshape(points[i]); //Reshape the point int he array into 3x1 vector
            
            mult_x(rotation_x, reshaped_vector); //Multiplies x-rotation matrix with reshaped vector and stores it in rotated_x vector

            mult_y(rotation_y,rotated_x); //Multiplies y-rotation matrix with rotated_x vector and stores it in product 3 vector

            mult_project(proj, rotated_y); //Multiply projection matrix with rotated vector

            //Points for vertext
            float x = projected2d[0][0]*scale + 480;
            float y = projected2d[1][0]*scale + 230;
            
            //Store projected points into matrix
            proj_points[i][0] = x;
            proj_points[i][1] = y;

            //Draw cube vertices
            fillCircle((short)x, (short)y, 3, BLUE);
        }

        //Connect cube vertices with lines
        for (int p = 0; p < 4; ++p) {
            connect_points(p, (p + 1) % 4);
            connect_points(p + 4, ((p + 1) % 4) + 4);
            connect_points(p, (p + 4));
        }

        // A brief nap
        sleep_ms(100) ;
}

////////////////////////////////////////////Threads////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
static PT_THREAD (protothread_trigpulse(struct pt *pt))
{    // Indicate start of thread
    PT_BEGIN(pt) ;
    while(1){
        //make 10 us sqaure wave pulse
        gpio_put(trig_pin, 0);
        sleep_ms(1);
        gpio_put(trig_pin, 1);
        sleep_us(10);
        gpio_put(trig_pin, 0);
        sleep_ms(10);
        PT_YIELD_usec(1) ;

    }

    PT_YIELD_usec(100000);
    // Indicate end of thread
    PT_END(pt);

}


// This thread runs on core 0
static PT_THREAD (protothread_core_0(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;
   

    while(1) {
        //can change so mode will change at any time in exec of code
        //based on button press

        //note: default of button is 1; when press button, change val to 0

        // Toggle on LED
        gpio_put(LED, 0) ;
        //gets the dist measured by the sensor
        dist = getCm(trig_pin, echo_pin);
         //button is pressed
        if((gpio_get(BUTTON)) == 0){
            //go into cont sound mode
            STATE_0 = 5;
            count_0 = 0;
            current_amplitude_0 = 0;   
            
            
        }
        if((gpio_get(BUTTON)) == 1){
            if( (dist < 60.0) && (dist > 45.0)){
             //play modified G-note
                STATE_0 = 2;
                count_0 = 0;
                current_amplitude_0 = 0;
                gpio_put(LED, 1) ;
            }
            if ( (dist < 45.0) && (dist > 30.0)) {
                //play modified D-note 
                STATE_0 = 3;
                count_0 = 0;
                current_amplitude_0 = 0;
                gpio_put(LED, 1);
            }
            if (dist < 30.0) {
                //play modified E-note
                STATE_0 = 4;
                count_0 = 0;
                current_amplitude_0 = 0;
                gpio_put(LED, 1);
            }
            //any other dist value will by default
            //play modified A-note


        }

        PT_YIELD_usec(30000) ;
                         
    }

       
        
    PT_YIELD_usec(30000) ;
    
    // Indicate thread end
    PT_END(pt) ;
}

// This timer ISR is called on core 0
bool repeating_timer_callback_core_0(struct repeating_timer *t) {
    //set GPIO 2 to high so know that we are in ISR
    gpio_put(2, 1) ;
    if (STATE_0 == 0) {
            //modifying A-note sound by creating a swoop by
            //changing the attack time
            A_note = amplitude*sin(440.0 * 3.14159 * count_0) + amplitude*sin(880 * 3.14159 * count_0)
                + amplitude*sin(1320.0 * 3.14159 * count_0)+amplitude*sin(1318.51 * 3.14159 * count_0)+amplitude*sin(2217.46 * 3.14159 * count_0);


            //increment the phase accumulator by the correct amount
            //then don't miss timing deadline of 130ms
            phase_accum_main_0 += (int)((A_note*two32)*2.5E-5);

            DAC_output_0 = fix2int15(multfix15(current_amplitude_0,
                sin_table[phase_accum_main_0>>24])) + 2048 ;

            // Ramp up amplitude
            if (count_0 < ATTACK_TIME) {
                current_amplitude_0 = (current_amplitude_0 + attack_inc) ;
            }
            // Ramp down amplitude
            else if (count_0 > SWOOP_DURATION - DECAY_TIME) {
                current_amplitude_0 = (current_amplitude_0 - decay_inc) ;
            }

            // Mask with DAC control bits
            DAC_data_0 = (DAC_config_chan_B | (DAC_output_0 & 0xffff))  ;

            // SPI write (no spinlock b/c of SPI buffer)
            spi_write16_blocking(SPI_PORT, &DAC_data_0, 1) ;

            // Increment the counter
            count_0 += 1 ;

            // State transition?
            if (count_0 == SWOOP_DURATION) {
                STATE_0 = 1 ;
                count_0 = 0 ;
            }

        }
        else if(STATE_0 == 2){
            //modify G-note sound by creating a chirp effect
            //by changing the value of the attack time
            G_note = amplitude*sin(196.0 * 3.14159 * count_0) + amplitude*sin(392.0 * 3.14159 * count_0)
            + amplitude*sin(784.0 * 3.14159 * count_0)+ amplitude*sin(293.665 * 3.14159 * count_0)+ amplitude*sin(1975.53 * 3.14159 * count_0);

            //increment the phase accumulator by the correct amount
            //then don't miss timing deadline of 130ms
            phase_accum_main_0 += (int)((G_note*two32)*2.5E-5);

            DAC_output_0 = fix2int15(multfix15(current_amplitude_0,
                sin_table[phase_accum_main_0>>24])) + 2048 ;

            // Ramp up amplitude
            if (count_0 < ATTACK_TIME) {
                current_amplitude_0 = (current_amplitude_0 + attack_inc) ;
            }
            // Ramp down amplitude
            else if (count_0 > CHIRP_DURATION - DECAY_TIME) {
                current_amplitude_0 = (current_amplitude_0 - decay_inc) ;
            }

            // Mask with DAC control bits
            DAC_data_0 = (DAC_config_chan_B | (DAC_output_0 & 0xffff))  ;

            // SPI write (no spinlock b/c of SPI buffer)
            spi_write16_blocking(SPI_PORT, &DAC_data_0, 1) ;

            // Increment the counter
            count_0 += 1 ;

            // State transition?
            if (count_0 == CHIRP_DURATION) {
                STATE_0 = 1 ;
                count_0 = 0 ;
            }
            
        }
        else if(STATE_0 == 3){
            //modify D-note sound  by creating a chirp effect
            //by changing the value of the attack time
            D_note = amplitude*sin(293.7 * 3.14159 * count_0) + amplitude*sin(587.4 * 3.14159 * count_0)
                + amplitude*sin(881.1 * 3.14159 * count_0)+ amplitude*sin(880.0 * 3.14159 * count_0)+ amplitude*sin(1479.98 * 3.14159 * count_0);

            //increment the phase accumulator by the correct amount
            //then don't miss timing deadline of 130ms
            phase_accum_main_0 += (int)((D_note * two32) * 2.5E-5);

            DAC_output_0 = fix2int15(multfix15(current_amplitude_0,
                sin_table[phase_accum_main_0 >> 24])) + 2048;

            // Ramp up amplitude
            if (count_0 < ATTACK_TIME) {
                current_amplitude_0 = (current_amplitude_0 + attack_inc);
            }
            // Ramp down amplitude
            else if (count_0 > CHIRP_DURATION - DECAY_TIME) {
                current_amplitude_0 = (current_amplitude_0 - decay_inc);
            }

            // Mask with DAC control bits
            DAC_data_0 = (DAC_config_chan_B | (DAC_output_0 & 0xffff));

            // SPI write (no spinlock b/c of SPI buffer)
            spi_write16_blocking(SPI_PORT, &DAC_data_0, 1);

            // Increment the counter
            count_0 += 1;

            // State transition?
            if (count_0 == CHIRP_DURATION) {
                STATE_0 = 1;
                count_0 = 0;
            }
            
        }
        else if (STATE_0 == 4) {
            //modify E-note sound  by creating a chirp effect
            //by changing the value of the attack time
            E_note = amplitude*sin(659.3 * 3.14159 * count_0) + amplitude*sin(1318.6 * 3.14159 * count_0)
                + amplitude*sin(1977.9 * 3.14159 * count_0)+ amplitude*sin(1975.53 * 3.14159 * count_0)+ amplitude*sin(3322.44 * 3.14159 * count_0);

            //increment the phase accumulator by the correct amount
            //then don't miss timing deadline of 130ms
            phase_accum_main_0 += (int)((E_note * two32) * 2.5E-5);

            DAC_output_0 = fix2int15(multfix15(current_amplitude_0,
                sin_table[phase_accum_main_0 >> 24])) + 2048;

            // Ramp up amplitude
            if (count_0 < ATTACK_TIME) {
                current_amplitude_0 = (current_amplitude_0 + attack_inc);
            }
            // Ramp down amplitude
            else if (count_0 > CHIRP_DURATION - DECAY_TIME) {
                current_amplitude_0 = (current_amplitude_0 - decay_inc);
            }

            // Mask with DAC control bits
            DAC_data_0 = (DAC_config_chan_B | (DAC_output_0 & 0xffff));

            // SPI write (no spinlock b/c of SPI buffer)
            spi_write16_blocking(SPI_PORT, &DAC_data_0, 1);

            // Increment the counter
            count_0 += 1;

            // State transition?
            if (count_0 == CHIRP_DURATION) {
                STATE_0 = 1;
                count_0 = 0;
            }


        }

        else if (STATE_0 == 5) { 
            //continuous sound
            //scale dist so that can use as input to change the freq and amplitude of the sine wave
            dist_scaled = dist*10;
            cont_sound = amplitude*dist*sin(3.14159 *dist_scaled);

            //increment the phase accumulator by the correct amount
            //then don't miss timing deadline of 130ms
            phase_accum_main_0 += (int)((cont_sound * two32) * 2.5E-5);

            DAC_output_0 = fix2int15(multfix15(current_amplitude_0,
                sin_table[phase_accum_main_0 >> 24])) + 2048;

            // Ramp up amplitude
            if (count_0 < ATTACK_TIME) {
                current_amplitude_0 = (current_amplitude_0 + attack_inc);
            }
            // Ramp down amplitude
            else if (count_0 > CONT_DURATION - DECAY_TIME) {
                current_amplitude_0 = (current_amplitude_0 - decay_inc);
            }

            // Mask with DAC control bits
            DAC_data_0 = (DAC_config_chan_B | (DAC_output_0 & 0xffff));

            // SPI write (no spinlock b/c of SPI buffer)
            spi_write16_blocking(SPI_PORT, &DAC_data_0, 1);

            // Increment the counter
            count_0 += 1;

            // State transition?
            if (count_0 == CONT_DURATION) {
                STATE_0 = 1;
                count_0 = 0;
            }

        }

    
    gpio_put(2, 0) ;

    return true;
}


// User input thread. User can change draw speed
static PT_THREAD(protothread_graphic(struct pt* pt))
{
    PT_BEGIN(pt);

    static char classifier;
    static int input_mode;

    while (1) {

    fillRect(0,0, 700, 700, BLACK);

        //Rotation matrices
        float rotation_z[3][3] = {{cos(angle), -sin(angle), 0}, {(sin(angle), cos(angle),0)}, {0,0,1}};
        float rotation_y[3][3] = {{cos(angle), 0, sin(angle)}, {0, 1, 0}, {-sin(angle), 0, cos(angle)}};
        float rotation_x[3][3] = {{1, 0, 0}, {0, cos(angle), -sin(angle)}, {0, sin(angle), cos(angle)}};
    
        //Gets all 8 cube vertices
        for(int i=0; i<8; i++){
            reshape(points[i]); //Reshape the point int he array into 3x1 vector
            
            multVect(rotation_x, reshaped_vector); //Multiplies x-rotation matrix with reshaped vector and stores it in rotated2d vector

            multVect3(rotation_y,rotated2d); //Multiplies y-rotation matrix with rotated2d vector and stores it in product 3 vector

            multVect2(proj, product3); //Multiply projection matrix with rotated vector

            //Points for vertext
            float x = projected2d[0][0]*scale + 480;
            float y = projected2d[1][0]*scale + 230;

            //Store projected points into matrix
            proj_points[i][0] = x;
            proj_points[i][1] = y;

            //Draw cube vertices
            fillCircle((short)x, (short)y, 3, BLUE);
        }

        //Connect cube vertices with lines
        for (int p = 0; p < 4; ++p) {
            connect_points(p, (p + 1) % 4);
            connect_points(p + 4, ((p + 1) % 4) + 4);
            connect_points(p, (p + 4));
        }

        //Increase angle
        if(prev_dist<=dist){
            angle = angle+0.1;
        }
        
        if(prev_dist>dist){
            angle = angle-0.1;
        }

        // A brief nap
        sleep_ms(50) ;
        prev_dist=dist;

        PT_YIELD_usec(10000);

    }
    PT_END(pt);
}




void core1_main(){
  // Add trigpulse thread
  pt_add_thread(protothread_trigpulse);
  // Start the scheduler
  pt_schedule_start ;

}



int main() {
    // Initialize stdio
    stdio_init_all();

    // Initialize the VGA screen
    initVGA() ;


    gpio_init(trig_pin);
    gpio_set_dir(trig_pin, GPIO_OUT) ;
    // Tell GPIO 12 that it is input -- trig
    gpio_init(echo_pin);
    gpio_set_dir(echo_pin, GPIO_IN) ;

     // start core 1 
     multicore_reset_core1();
     multicore_launch_core1(&core1_main);


     // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000) ;
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;

    // Map LDAC pin to GPIO port, hold it low (could alternatively tie to GND)
    gpio_init(LDAC) ;
    gpio_set_dir(LDAC, GPIO_OUT) ;
    gpio_put(LDAC, 0) ;

    // Map LED to GPIO port, make it low
    gpio_init(LED) ;
    gpio_set_dir(LED, GPIO_OUT) ;
    gpio_put(LED, 1) ;

     // Map LED to GPIO port, make it low
    gpio_init(2) ;
    gpio_set_dir(2, GPIO_OUT) ;

    //Make GPIO 10 an output pin to see if get
    //into function getCm()
    gpio_init(10) ;
    gpio_set_dir(10, GPIO_OUT) ;

   

    // set up increments for calculating bow envelope
    attack_inc = divfix(max_amplitude, int2fix15(ATTACK_TIME)) ;
    decay_inc =  divfix(max_amplitude, int2fix15(DECAY_TIME)) ;

    // Build the sine lookup table
    // scaled to produce values between 0 and 4096 (for 12-bit DAC)
    int ii;
    for (ii = 0; ii < sine_table_size; ii++){
         sin_table[ii] = float2fix15(2047*sin((float)ii*6.283/(float)sine_table_size));
    }

    // Create a repeating timer that calls 
    // repeating_timer_callback (defaults core 0)
    struct repeating_timer timer_core_0;

    // Negative delay so means we will call repeating_timer_callback, and call it
    // again 25us (40kHz) later regardless of how long the callback took to execute
    add_repeating_timer_us(-25, 
        repeating_timer_callback_core_0, NULL, &timer_core_0);

    // Map LED to GPIO port, make it low
    gpio_init(LED) ;
    gpio_set_dir(LED, GPIO_OUT) ;
    gpio_put(LED, 1) ;


    // Add core 0 threads
    pt_add_thread(protothread_core_0) ;
    pt_add_thread(protothread_graphic);


     
 

  // === config threads ===================
  // for core 0
    pt_schedule_start ;

}
