#include "vga_graphics.h"
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
// Include Pico libraries
#include "pico/stdlib.h"
#include "pico/multicore.h"
// Include hardware libraries
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/adc.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "hardware/sync.h"
// Include protothreads
#include "pt_cornell_rp2040_v1.h"

// Include SD Libraries
#include "hw_config.h"
#include "ff.h"
#include "sd_card.h"

#define LED_PIN 25

// === the fixed point macros (16.15) ========================================
typedef signed int fix15 ;
#define multfix15(a,b) ((fix15)((((signed long long)(a))*((signed long long)(b)))>>15))
#define float2fix15(a) ((fix15)((a)*32768.0)) // 2^15
#define fix2float15(a) ((float)(a)/32768.0)
#define absfix15(a) abs(a) 
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a,b) (fix15)( (((signed long long)(a)) << 15) / (b))

/////////////////////////// ADC configuration ////////////////////////////////
// ADC Channel and pin
// input audio jack
#define ADC_CHAN_0 0 
// potentiometers
#define ADC_CHAN_1 1 
#define ADC_CHAN_2 2

// input audio jack
#define ADC_PIN 26
// potentiometers
#define PIN_ADC_1 27
#define PIN_ADC_2 28

// Number of samples per FFT
#define NUM_SAMPLES 1024
// Number of samples per FFT, minus 1
#define NUM_SAMPLES_M_1 1023
// Length of short (16 bits) minus log2 number of samples (10)
#define SHIFT_AMOUNT 6
// Log2 number of samples
#define LOG2_NUM_SAMPLES 10
// Sample rate (Hz)
#define Fs 12500.0
// Interrupt interval
#define INTERVAL -80
// ADC clock rate (unmutable!)
#define ADCCLK 48000000.0

// DMA channels for sampling ADC (VGA driver uses 0 and 1)
int sample_chan = 2 ;
int control_chan = 3 ;

// Max and min macros
#define max(a,b) ((a>b)?a:b)
#define min(a,b) ((a<b)?a:b)

// 0.4 in fixed point (used for alpha max plus beta min)
fix15 zero_point_4 = float2fix15(0.4) ;

// Here's where we'll have the DMA channel put ADC samples
uint8_t sample_array[NUM_SAMPLES] ;
// And here's where we'll copy those samples for FFT calculation
fix15 fr[NUM_SAMPLES] ;
fix15 fi[NUM_SAMPLES] ;

// Sine table for the FFT calculation
fix15 Sinewave[NUM_SAMPLES]; 

// for testing
fix15 Sinewave_2[NUM_SAMPLES]; 

// Pointer to address of start of sample buffer
uint8_t * sample_address_pointer = &sample_array[0] ;

// Interrupt State machine variables
volatile unsigned int STATE_0 = 0 ;
#define PLAYBACK 0
#define PLAYBACK_DONE 1
#define QUIET 2 //while it's recording
volatile unsigned int count = 0 ;

// SPI data
uint16_t DAC_data_1 ; // output value
uint16_t DAC_data_0 ; // output value

//SPI configurations (note these represent GPIO number, NOT pin number)
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define LDAC     8
#define LED      25
#define SPI_PORT spi0

// DAC parameters (see the DAC datasheet)
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

// sound variables
fix15 frame_arr[4096][1000];
volatile fix15 sound;
volatile int sound_idx = 0;
volatile int interrupt_idx;

// we output processed audio data from this array
fix15 interrupt_fft_array[NUM_SAMPLES];

// we read audio input into these arrays
fix15 fft_buff_r[NUM_SAMPLES];
fix15 fft_buff_i[NUM_SAMPLES];

volatile int corenum_0  ;

void IFFTfix(fix15 fr[], fix15 fi[]);
void FFTfix(fix15 fr[], fix15 fi[]);

char buf[100];

// potentiometer variables
// potentiometer inputs from adc channels are stored here 
int chan_1_reading;
int chan_2_reading;

// we scale the adc input and use these values to scale
// the audio in the frequency domain
float s1;
float s2;

// SEMAPHORES
static struct pt_sem fft_semaphore;
static struct pt_sem load_semaphore;

// vga
int color_array[5] = {WHITE, GREEN, YELLOW, MAGENTA, CYAN};

// This timer ISR is called on core 0
bool repeating_timer_callback_core_0(struct repeating_timer *t) {
    // we sometimes use this gpio to check our interrupt timing on the oscilloscope
    // gpio_put(14,0);

    // COLLECT SAMPLES FROM INPUT HEADPHONE JACK (adc channel)
    if (interrupt_idx < NUM_SAMPLES){
        adc_select_input(ADC_CHAN_0);
        fft_buff_r[interrupt_idx] = int2fix15(adc_read()) ;
        fft_buff_i[interrupt_idx] = (fix15) 0;
        interrupt_idx++;
    }
    else {
        // copy the new frame into fr and fi arrays for processing
        // in fft thread
        memcpy(fr, fft_buff_r, sizeof(fix15) * NUM_SAMPLES);
        memcpy(fi, fft_buff_i, sizeof(fix15) * NUM_SAMPLES);

        // reset the index so we can start collecting more data from 
        // the adc channel
        interrupt_idx = 0;

        // signal to thread that the new frame is ready for processing
        PT_SEM_SIGNAL(pt, &fft_semaphore);
    }

    // OUTPUT THE ALTERED AUDIO FRAME TO THE DAC 
    sound = interrupt_fft_array[sound_idx];  

    sound_idx++ ;

    // we are done outputting this frame
    if (sound_idx >= NUM_SAMPLES) {
        sound_idx = 0;
        // signal to the fft thread that we are done outputting this frame
        // so fft thread can load the next computed frame into the output array 
        // (interrupt_fft_array)
        PT_SEM_SIGNAL(pt, &load_semaphore);
    }

    // send the data out to the dac!
    DAC_data_0 = DAC_config_chan_B | ((fix2int15(multfix15(sound, int2fix15(2))) + 2048) & 0xffff)  ;
    DAC_data_1 = DAC_config_chan_A | ((fix2int15(multfix15(sound, int2fix15(2))) + 2048) & 0xffff)  ;

    spi_write16_blocking(SPI_PORT, &DAC_data_0, 1) ;
    spi_write16_blocking(SPI_PORT, &DAC_data_1, 1) ;

    gpio_put(14,1);

    // retrieve core number of execution
    // gpio_put(14, 1) ;
    corenum_0 = get_core_num() ;
    return true;
    
}

// Runs on core 1
static PT_THREAD (protothread_fft(struct pt *pt))
{
    // Indicate beginning of thread
    PT_BEGIN(pt) ;

    char str[50];

    while(1) {
        // wait for the interrupt to signal that a frame is ready to be operated on
        PT_SEM_WAIT(pt, &fft_semaphore); 

        // operate
        FFTfix(fr, fi);

        // apply potentiometer inputs 
        adc_select_input(ADC_CHAN_1) ;
        chan_1_reading = adc_read();
        adc_select_input(ADC_CHAN_2) ;
        chan_2_reading = adc_read();

        // scale the adc inputs so they are in a usable range
        s1 = 3.3 * chan_1_reading / 4096. - 1.1;
        s2 = 3.3 * chan_2_reading / 4096. - 1.1;

        for (int x = 0; x < NUM_SAMPLES; x++) {
        
        // fully filter out the lowest frequencies
        if (x < 2) { 
            fr[x] = (fix15) 0;
            fi[x] = (fix15) 0;
        }
        // scale frequencies from idx 2-99 using the first potentiometer
        else if (x < 100) {
            fr[x] = multfix15(float2fix15(s1), fr[x]);
            fi[x] = multfix15(float2fix15(s1), fi[x]);
        }
        // scale frequencies from idx 100-1024 using the second potentiometer
        else {
            fr[x] = multfix15(float2fix15(s2), fr[x]);
            fi[x] = multfix15(float2fix15(s2), fi[x]);
        }
    }

        // perform the ifft!
        IFFTfix(fr, fi);

        // wait for the interrupt to signal that the prior frame has finished being outputted
        PT_SEM_WAIT(pt, &load_semaphore);

        // load new frame into the output array
        memcpy(interrupt_fft_array, fr, sizeof(fix15) * NUM_SAMPLES);
    }
    PT_END(pt) ;
}

static PT_THREAD (protothread_vga(struct pt *pt))
{
    // Indicate beginning of thread
    PT_BEGIN(pt) ;

    static int height;
    static int color_idx = 0;

    // Write some text to VGA
    setTextColor(WHITE) ;
    setCursor(65, 0) ;
    setTextSize(1) ;
    writeString("Raspberry Pi Pico") ;
    
    setCursor(65, 60) ;
    writeString("Mattie, Jacob, Michael") ;

    setTextSize(3) ;
    setCursor(65, 20) ;
    writeString("DJ 2040") ;
    setTextSize(2) ;
    setCursor(275, 15) ;
    writeString("LOWS: ") ;
    setCursor(275, 45) ;
    writeString("HIGHS: ") ;

    while (1) {
        
        // draw some bars for the potentiometers     
        fillRect(350, 10, 300, 20, BLACK);
        fillRect(350, 10, s1 * 100, 20, WHITE);
        
        fillRect(350, 40, 300, 20, BLACK);
        fillRect(350, 40, s2 * 100, 20, WHITE);

        int k=5;
        for (int j=5; j<(NUM_SAMPLES); j+=2) {

            // draw the vt graph on the vga
            drawVLine(59+k, 100, 429, BLACK);
            height = fix2int15(divfix(interrupt_fft_array[j], int2fix15(8))) ; 
            drawVLine(59+k, 300-height, 2, color_array[color_idx]);
            k++;
        }
        // rotate through some fun colors
        if (color_idx < 4) color_idx++;
        else color_idx = 0;

        PT_YIELD_usec(30000) ;
    }
    PT_END(pt) ;
}

// Core 1 entry point (main() for core 1)
void core1_main() {
    // Add and schedule threads
    pt_add_thread(protothread_fft) ;
    pt_schedule_start ;
}

int main() {

    stdio_init_all();

    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000) ;
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);

    // Map LDAC pin to GPIO port, hold it low (could alternatively tie to GND)
    gpio_init(LDAC) ;
    gpio_set_dir(LDAC, GPIO_OUT) ;
    gpio_put(LDAC, 0) ;

    // Initialize the VGA screen
    initVGA() ;

    // Map LED to GPIO port, make it low
    gpio_init(LED) ;
    gpio_set_dir(LED, GPIO_OUT) ;
    gpio_put(LED, 0) ;

    // Record Button GPIO
    gpio_init(15);
    gpio_set_dir(15, GPIO_IN);
    gpio_pull_up(15);

    gpio_init(14);
    gpio_set_dir(14, GPIO_OUT);
    gpio_put(14, 1);

    ///////////////////////////////////////////////////////////////////////////////
    // ============================== ADC CONFIGURATION ===========================
    ///////////////////////////////////////////////////////////////////////////////
    // Init GPIO for analogue use: hi-Z, no pulls, disable digital input buffer.
    // adc_gpio_init(ADC_PIN);
    adc_gpio_init(ADC_PIN);
    adc_gpio_init(PIN_ADC_1); //26
    adc_gpio_init(PIN_ADC_2); //28


    // Initialize the ADC harware
    // (resets it, enables the clock, spins until the hardware is ready)
    adc_init() ;

    // Divisor of 0 -> full speed. Free-running capture with the divider is
    // equivalent to pressing the ADC_CS_START_ONCE button once per `div + 1`
    // cycles (div not necessarily an integer). Each conversion takes 96
    // cycles, so in general you want a divider of 0 (hold down the button
    // continuously) or > 95 (take samples less frequently than 96 cycle
    // intervals). This is all timed by the 48 MHz ADC clock. This is setup
    // to grab a sample at 25kHz (48Mhz/10kHz - 1)
    adc_set_clkdiv(ADCCLK/Fs);

    adc_run(false) ;

    // Populate the sine table
    int ii;
    for (ii = 0; ii < NUM_SAMPLES; ii++) {
        Sinewave_2[ii] = float2fix15(sin(6.283 * ((float) ii) / (float)(NUM_SAMPLES/8)));
        Sinewave[ii] = float2fix15(sin(6.283 * ((float) ii) / (float)NUM_SAMPLES));    }

    // Create a repeating timer that calls 
    // repeating_timer_callback (defaults core 0)
    struct repeating_timer timer_core_0;

    // Negative delay so means we will call repeating_timer_callback, and call it
    // again one INTERVAL later regardless of how long the callback took to execute
    add_repeating_timer_us(INTERVAL, 
        repeating_timer_callback_core_0, NULL, &timer_core_0);

    // Launch core 1
    multicore_launch_core1(core1_main);

    // Add and schedule core 0 threads
    pt_add_thread(protothread_vga) ;
    pt_schedule_start ;

    return 0;
}

///////////////////////////// INVERSE FFT IN-PLACE ///////////////////////////////////
void IFFTfix(fix15 fr[], fix15 fi[]) {
    unsigned short m;   // one of the indices being swapped
    unsigned short mr ; // the other index being swapped (r for reversed)
    fix15 tr, ti ; // for temporary storage while swapping, and during iteration
    
    int i, j ; // indices being combined in Danielson-Lanczos part of the algorithm
    int L ;    // length of the FFT's being combined
    int k ;    // used for looking up trig values from sine table
    
    int istep ; // length of the FFT which results from combining two FFT's
    
    fix15 wr, wi ; // trigonometric values from lookup table
    fix15 qr, qi ; // temporary variables used during DL part of the algorithm

    //////////////////////////////////////////////////////////////////////////
    ////////////////////////// BIT REVERSAL //////////////////////////////////
    //////////////////////////////////////////////////////////////////////////
    // Bit reversal code below based on that found here: 
    // https://graphics.stanford.edu/~seander/bithacks.html#BitReverseObvious

    // take out the extra frequency that we added when calculating the fft


    for (m=1; m<NUM_SAMPLES_M_1; m++) {
        // swap odd and even bits
        mr = ((m >> 1) & 0x5555) | ((m & 0x5555) << 1);
        // swap consecutive pairs
        mr = ((mr >> 2) & 0x3333) | ((mr & 0x3333) << 2);
        // swap nibbles ... 
        mr = ((mr >> 4) & 0x0F0F) | ((mr & 0x0F0F) << 4);
        // swap bytes
        mr = ((mr >> 8) & 0x00FF) | ((mr & 0x00FF) << 8);
        // shift down mr
        mr >>= SHIFT_AMOUNT ;
        // don't swap that which has already been swapped
        if (mr<=m) continue ;
        // swap the bit-reveresed indices
        tr = fr[m] ;
        fr[m] = fr[mr] ;
        fr[mr] = tr ;
        ti = fi[m] ;
        fi[m] = fi[mr] ;
        fi[mr] = ti ;
        

    }

    //////////////////////////////////////////////////////////////////////////
    ////////////////////////// Danielson-Lanczos //////////////////////////////
    //////////////////////////////////////////////////////////////////////////
    // Adapted from code by:
    // Tom Roberts 11/8/89 and Malcolm Slaney 12/15/94 malcolm@interval.com
    // Length of the FFT's being combined (starts at 1)
    L = 1 ;
    // Log2 of number of samples, minus 1
    k = LOG2_NUM_SAMPLES - 1 ;
    // While the length of the FFT's being combined is less than the number 
    // of gathered samples . . .
    while (L < NUM_SAMPLES) {
        // Determine the length of the FFT which will result from combining two FFT's
        istep = L<<1 ;
        // For each element in the FFT's that are being combined . . .
        for (m=0; m<L; ++m) { 
            // Lookup the trig values for that element
            j = m << k ;                         // index of the sine table
            wr =  Sinewave[j + NUM_SAMPLES/4] ; // cos(2pi m/N)
            wi = Sinewave[j] ;                 //    Sign not reversed for ifft       sin(2pi m/N)
            wr >>= 1 ;                          // divide by two
            wi >>= 1 ;                          // divide by two
            // i gets the index of one of the FFT elements being combined
            for (i=m; i<NUM_SAMPLES; i+=istep) {
                // j gets the index of the FFT element being combined with i
                j = i + L ;
                // compute the trig terms (bottom half of the above matrix)
                tr = multfix15(wr, fr[j]) - multfix15(wi, fi[j]) ;
                ti = multfix15(wr, fi[j]) + multfix15(wi, fr[j]) ;
                // DO NOT divide ith index elements by two (top half of above matrix)
                qr = fr[i];
                qi = fi[i];
                
                // compute the new values at each index
                fr[j] = qr - tr ;
                fi[j] = qi - ti ;
                fr[i] = qr + tr ;
                fi[i] = qi + ti ;
            }    
        }
        --k ;
        L = istep ;
    }
}


///////////////////////////// FFT IN-PLACE ///////////////////////////////////
// Peforms an in-place FFT. For more information about how this
// algorithm works, please see https://vanhunteradams.com/FFT/FFT.html
void FFTfix(fix15 fr[], fix15 fi[]) {
    
    unsigned short m;   // one of the indices being swapped
    unsigned short mr ; // the other index being swapped (r for reversed)
    fix15 tr, ti ; // for temporary storage while swapping, and during iteration
    
    int i, j ; // indices being combined in Danielson-Lanczos part of the algorithm
    int L ;    // length of the FFT's being combined
    int k ;    // used for looking up trig values from sine table
    
    int istep ; // length of the FFT which results from combining two FFT's
    
    fix15 wr, wi ; // trigonometric values from lookup table
    fix15 qr, qi ; // temporary variables used during DL part of the algorithm
    
    //////////////////////////////////////////////////////////////////////////
    ////////////////////////// BIT REVERSAL //////////////////////////////////
    //////////////////////////////////////////////////////////////////////////
    // Bit reversal code below based on that found here: 
    // https://graphics.stanford.edu/~seander/bithacks.html#BitReverseObvious
    for (m=1; m<NUM_SAMPLES_M_1; m++) {
        // swap odd and even bits
        mr = ((m >> 1) & 0x5555) | ((m & 0x5555) << 1);
        // swap consecutive pairs
        mr = ((mr >> 2) & 0x3333) | ((mr & 0x3333) << 2);
        // swap nibbles ... 
        mr = ((mr >> 4) & 0x0F0F) | ((mr & 0x0F0F) << 4);
        // swap bytes
        mr = ((mr >> 8) & 0x00FF) | ((mr & 0x00FF) << 8);
        // shift down mr
        mr >>= SHIFT_AMOUNT ;
        // don't swap that which has already been swapped
        if (mr<=m) continue ;
        // swap the bit-reveresed indices
        tr = fr[m] ;
        fr[m] = fr[mr] ;
        fr[mr] = tr ;
        ti = fi[m] ;
        fi[m] = fi[mr] ;
        fi[mr] = ti ;
    }
    //////////////////////////////////////////////////////////////////////////
    ////////////////////////// Danielson-Lanczos //////////////////////////////
    //////////////////////////////////////////////////////////////////////////
    // Adapted from code by:
    // Tom Roberts 11/8/89 and Malcolm Slaney 12/15/94 malcolm@interval.com
    // Length of the FFT's being combined (starts at 1)
    L = 1 ;
    // Log2 of number of samples, minus 1
    k = LOG2_NUM_SAMPLES - 1 ;
    // While the length of the FFT's being combined is less than the number 
    // of gathered samples . . .
    while (L < NUM_SAMPLES) {
        // Determine the length of the FFT which will result from combining two FFT's
        istep = L<<1 ;
        // For each element in the FFT's that are being combined . . .
        for (m=0; m<L; ++m) { 
            // Lookup the trig values for that element
            j = m << k ;                         // index of the sine table
            wr =  Sinewave[j + NUM_SAMPLES/4] ; // cos(2pi m/N)
            wi = -Sinewave[j] ;                 // sin(2pi m/N)
            wr >>= 1 ;                          // divide by two
            wi >>= 1 ;                          // divide by two
            // i gets the index of one of the FFT elements being combined
            for (i=m; i<NUM_SAMPLES; i+=istep) {
                // j gets the index of the FFT element being combined with i
                j = i + L ;
                // compute the trig terms (bottom half of the above matrix)
                tr = multfix15(wr, fr[j]) - multfix15(wi, fi[j]) ;
                ti = multfix15(wr, fi[j]) + multfix15(wi, fr[j]) ;
                // divide ith index elements by two (top half of above matrix)
                qr = fr[i] >> 1;
                qi = fi[i] >> 1;
                // compute the new values at each index
                fr[j] = qr - tr ;
                fi[j] = qi - ti ;
                fr[i] = qr + tr ;
                fi[i] = qi + ti ;
            }    
        }
        --k ;
        L = istep ;
    }
}
