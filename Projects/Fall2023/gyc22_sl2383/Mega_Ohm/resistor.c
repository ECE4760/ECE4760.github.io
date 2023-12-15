/**
 * Giacomo Yong Cuomo (gyc22)  -  Sophia Lin (sl2383)
 * 
 * The C file resistor.c containing all the code used to light up the color lamp, display the resistance
 * and audio output the numerical value through a speaker.
 * 
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/spi.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/pio.h"

// Include protothreads
#include "pt_cornell_rp2040_v1.h"
// Include TFT
#include "TFTMaster.h"
// Include LED
#include "LED.h"
// Include sounds arrays
#include "sounds_arrays.h"

// GPIO connections for DAC and Display
#define DAC_CS 1
#define MISO 4
#define DAC_SCK 2
#define DAC_MOSI 3
#define LDAC 8
#define SPI_PORT spi0   //SPI port
#define DAC_config_chan_A 0b0011000000000000    // A-channel, 1x, active

// GPIO connections for display (some are also defined in TFTMaster.h)
#define DISPLAY_CS 5
#define SCK 6
#define MOSI 7

// GPIO connections for the buttons
#define BUTTON_3 10
#define BUTTON_4 11
#define BUTTON_5 12
#define BUTTON_6 13

//GPIO connection for voltage measurements
#define ADC_0 26

// PWM parameters for ISR
#define WRAPVAL 5000
#define CLKDIV  25.0f
uint slice_num;

//LED
#define LED_PIN 18
#define LED_PIN_BORDER 28
#define LED_NUM 110

// Semaphore for display thread
static struct pt_sem display_semaphore;

// ADC constants
const float conversion_factor = 3.3f / (1 << 12);
float voltage_out;
const float resistor_1 = 10000;
const float voltage_in = 3.22;
float resistance;
unsigned volatile int audio_res_int;
char audio_res_to_fire[50];
unsigned volatile int audio_res_to_fire_size;

// Display constants
char resistance_text[100];
char series_text[100] = " No Series.";
char audio_text[100] = "Audio not selected.";

// SPI data
uint16_t DISP_data_1;  // output value
uint16_t DISP_data_0;  // output value

// Buttons constants
volatile unsigned int state_pressed;
volatile unsigned int button;
volatile unsigned int series;
volatile unsigned int audio;

// DMA Channel and constant
int sound_channel = 0;
dma_channel_config c0;

// Struct for each color to be sent to the LEDs
struct color{
    int r;
    int g; 
    int b;
};

// LEDs constants
unsigned volatile int led_res_int;
unsigned volatile int led_rounded_int;
unsigned volatile int tolerance;
char led_res_to_fire[50];
unsigned volatile int led_res_to_fire_size;
struct color bands[5];
unsigned volatile int red = 255;
unsigned volatile int green = 0;
unsigned volatile int blue = 255;

// LED to light up (start at 0)
// 1 4 7 10 13 (series 5)
// 2-3 5-6 8-9 11-12 (series 4)
// 4 7 10 (series 3)

// Interrupt Service Routine looking to voltage divider
void ISR() {
    // Clear the interrupt flag that brought us here
    pwm_clear_irq(pwm_gpio_to_slice_num(0));
    // Raw voltage measurements from voltage divider
    voltage_out = adc_read()*conversion_factor;  // [0,4096] -> [0,3.3]
    // Calculate resistance of input
    resistance = (voltage_out*resistor_1)/(voltage_in - voltage_out);
    // Release semaphore for display thread
    PT_SEM_SIGNAL(pt, &display_semaphore);
}


// This thread runs on core 0
static PT_THREAD(protothread_display(struct pt *pt)) {
    // Indicate thread beginning
    PT_BEGIN(pt);
    // Print the sentence "Resistance: " for the display
    tft_setCursor(100, 30);
    tft_setTextColor2(ILI9340_WHITE, ILI9340_BLACK);
    tft_setTextSize(2);
    tft_writeString("Resistance:");

    // Wait on semaphore
    PT_SEM_WAIT(pt, &display_semaphore);

    // Convert resistance input from a float to a char list
    sprintf(resistance_text, "%f", resistance);
    // Print resistance input on the display
    tft_setCursor(80, 60);
    tft_writeString(resistance_text);
    // Print series type input on the display
    tft_setCursor(100, 110);
    tft_writeString(series_text);
    // Print if audio is selected or not
    tft_setCursor(55, 160);
    tft_writeString(audio_text);
    PT_YIELD_usec(300000);
    // Indicate thread end
    PT_END(pt);
}

// Series by buttons
static PT_THREAD(protothread_button(struct pt *pt)) {
    // Indicate thread beginning
    // Some variables
    static int possible;
    
    PT_BEGIN(pt);
    while (1) {
        //scan buttons
        if (gpio_get(BUTTON_3) == 0) (button = 3);
        else if (gpio_get(BUTTON_4) == 0) (button = 4);
        else if (gpio_get(BUTTON_5) == 0) (button = 5);
        else if (gpio_get(BUTTON_6) == 0) (button = 6);
        else (button = -1);

        // Check which state we are in
        if (state_pressed == 0) {   // Button has not being pressed
                if (button != -1) {possible = button; state_pressed = 1;}
            }
        if (state_pressed == 1) {   // Button has maybe being pressed
            if (button == possible) {state_pressed = 2; set_all(204,255,255); send_led_data(LED_PIN);}
            else {state_pressed = 0;}
        }
        if (state_pressed == 2) {   // Button has for sure being pressed
            // Set series variable to what is desired to show depending on the button and computes the instantenous resistance
            if (button == 3) {
                sprintf(series_text, "%s", " Series 3.           ");
                series = 3;
                led_res_int = (int) resistance;     // Convert resistance to int and store it in an array to read it
            }
            if (button == 4) {
                sprintf(series_text, "%s", " Series 4.           ");
                series = 4;
                led_res_int = (int) resistance;     // Convert resistance to int and store it in an array to read it
            }
            if (button == 5) {
                sprintf(series_text, "%s", " Series 5.           ");
                series = 5;
                led_res_int = (int) resistance;     // Convert resistance to int and store it in an array to read it
            }
            if (button == 6) {
                sprintf(audio_text, "%s", "  Audio selected.     ");   // Audio is selected
                audio = 1;
                audio_res_int = (int) resistance;       // Convert resistance to int and store it in an array to read it
                sprintf(audio_res_to_fire, "%d", audio_res_int);
                audio_res_to_fire_size = (audio_res_int == 0) ? 0 : (int)(log10(fabs(audio_res_int))+1);     // calculate order of magnitude of resistance
            }
            if (button != possible) {state_pressed = 3;}
        }
        if (state_pressed == 3) {   // Button is maybe unpressed
            if (button == possible) {state_pressed = 2;}
            else {state_pressed = 0;}
        }
        PT_YIELD_usec(30000);
    }
    // Indicate thread end
    PT_END(pt);
}

// Calculates the color of a given band using ASCII entries and sets it
void band_color(int num, int i){
    if (num == 48) { bands[i].r = 0; bands[i].g = 0; bands[i].b = 0;}
    else if (num == 49) {bands[i].r = 102; bands[i].g = 51; bands[i].b = 0;}
    else if (num == 50) {bands[i].r = 255; bands[i].g = 0; bands[i].b = 0;}
    else if (num == 51) {bands[i].r = 255; bands[i].g = 128; bands[i].b = 0;}
    else if (num == 52) {bands[i].r = 255; bands[i].g = 255; bands[i].b = 0;}
    else if (num == 53) {bands[i].r = 0; bands[i].g = 255; bands[i].b = 0;}
    else if (num == 54) {bands[i].r = 0; bands[i].g = 0; bands[i].b = 255;}
    else if (num == 55) {bands[i].r = 127; bands[i].g = 0; bands[i].b = 255;}
    else if (num == 56) {bands[i].r = 128; bands[i].g = 128; bands[i].b = 128;}
    else if (num == 57) {bands[i].r = 255; bands[i].g = 255; bands[i].b = 255;}
}

// Color coding to LEDs
static PT_THREAD(protothread_leds(struct pt *pt)) {
    // Indicate thread beginning
    PT_BEGIN(pt);
    while (1) {
        int power = (led_res_int == 0) ? 0 : (int)log10(fabs(led_res_int));     // calculate order of magnitude of resistance
        int multiplier;
        if (power >= 1) {multiplier = pow(10, power-1);}
        if (series == 3) {
            led_rounded_int = round(led_res_int/(double)multiplier)*multiplier;     // calculate resistance rounded version
            sprintf(led_res_to_fire, "%d", led_rounded_int);
            for(int i = 0; i < 2; i++){
                band_color(led_res_to_fire[i], i);
            }
            band_color(power+47, 2);

            // Set each led to the relative band color
            set_led(4, bands[0].r, bands[0].g, bands[0].b);
            set_led(7, bands[1].r, bands[1].g, bands[1].b);
            set_led(10, bands[2].r, bands[2].g, bands[2].b);
            set_led(0,0,0,0); set_led(1,0,0,0); set_led(2,0,0,0); set_led(3,204,255,255); set_led(5,204,255,255); set_led(6,204,255,255);
            set_led(8,204,255,255); set_led(9,204,255,255); set_led(11,204,255,255); set_led(12,0,0,0); set_led(13,0,0,0); set_led(14,0,0,0);
        }
        else if (series == 4) {
            led_rounded_int = round(led_res_int/(double)multiplier)*multiplier;     // calculate resistance rounded version
            tolerance = abs(ceil(((led_res_int - led_rounded_int)/(double)led_rounded_int)*100));   // calculate tolerance
            sprintf(led_res_to_fire, "%d", led_rounded_int);
            for(int i = 0; i < 2; i++){
                band_color(led_res_to_fire[i], i);
            }
            band_color(power+47, 2);
            band_color(tolerance+48, 3);

            // Set each led to the relative band color
            set_led(2, bands[0].r, bands[0].g, bands[0].b);
            set_led(3, bands[0].r, bands[0].g, bands[0].b);
            set_led(5, bands[1].r, bands[1].g, bands[1].b);
            set_led(6, bands[1].r, bands[1].g, bands[1].b);
            set_led(8, bands[2].r, bands[2].g, bands[2].b);
            set_led(9, bands[2].r, bands[2].g, bands[2].b);
            set_led(11, bands[3].r, bands[3].g, bands[3].b);
            set_led(12, bands[3].r, bands[3].g, bands[3].b);
            set_led(0,0,0,0); set_led(14,0,0,0);
            set_led(1,204,255,255); set_led(4,204,255,255); set_led(7,204,255,255); set_led(10,204,255,255); set_led(13,204,255,255);
        }
        else if (series == 5) {
            led_rounded_int = round(led_res_int/(double)(multiplier-1))*(multiplier-1);     // calculate resistance rounded version
            tolerance = abs(ceil(((led_res_int - led_rounded_int)/(double)led_rounded_int)*100));   // calculate tolerance
            for(int i = 0; i < 3; i++){
                band_color(led_res_to_fire[i], i);
            }
            band_color(power+46, 3);
            band_color(tolerance+48, 4);

            // Set each led to the relative band color
            set_led(1, bands[0].r, bands[0].g, bands[0].b);
            set_led(4, bands[1].r, bands[1].g, bands[1].b);
            set_led(7, bands[2].r, bands[2].g, bands[2].b);
            set_led(10, bands[3].r, bands[3].g, bands[3].b);
            set_led(13, bands[4].r, bands[4].g, bands[4].b);
            set_led(0,204,255,255); set_led(2,204,255,255); set_led(3,204,255,255); set_led(5,204,255,255); set_led(6,204,255,255);
            set_led(8,204,255,255); set_led(9,204,255,255); set_led(11,204,255,255); set_led(12,204,255,255); set_led(14,204,255,255); 
        }
        send_led_data(LED_PIN);
        
        // Set border pins to a wave color
        if (red == 255) (red = 0); else (red += 1);
        if (green == 255) (green = 0); else (green += 1);
        if (blue == 255) (blue = 0); else (blue += 1);
        set_all(red, green, blue); send_led_data(LED_PIN_BORDER);
        PT_YIELD_usec(30000);
    }
    // Indicate thread end
    PT_END(pt);
}

// Audio firing
static PT_THREAD(protothread_audio(struct pt *pt)) {
    // Indicate thread beginning
    PT_BEGIN(pt);
    while (1) {
        if (audio) {
            // Fire DMA Channel (Sound_Intro)
            dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_Intro, 53288, true);
            PT_YIELD_usec(1500000);     // Yield for 1.5sec
            // Fire each DMA Channel if needed (Sound_0 -> Sound_9) and fire Sound_Ohms
            for (int i = 0; i < audio_res_to_fire_size+1; i++) {
                if (audio_res_to_fire[i] == 48) {     //check if it's a zero
                    dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_0, 22050, true);
                    sleep_ms(700);     // wait 700 msec
                }
                else if (audio_res_to_fire[i] == 49) {    //check if it's a one
                    dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_1, 16538, true);
                    sleep_ms(700);     // wait 700 msec
                }
                else if (audio_res_to_fire[i] == 50) {    //check if it's a two
                    dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_2, 14700, true);
                    sleep_ms(700);     // wait 700 msec
                }
                else if (audio_res_to_fire[i] == 51) {    //check if it's a three
                    dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_3, 16538, true);
                    sleep_ms(700);     // wait 700 msec
                }
                else if (audio_res_to_fire[i] == 52) {    //check if it's a four
                    dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_4, 16538, true);
                    sleep_ms(700);     // wait 700 msec
                }
                else if (audio_res_to_fire[i] == 53) {    //check if it's a five
                    dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_5, 18375, true);
                    sleep_ms(700);     // wait 700 msec
                }
                else if (audio_res_to_fire[i] == 54) {    //check if it's a six
                    dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_6, 23888, true);
                    sleep_ms(700);     // wait 700 msec
                }
                else if (audio_res_to_fire[i] == 55) {    //check if it's a seven
                    dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_7, 20213, true);
                    sleep_ms(700);     // wait 700 msec
                }
                else if (audio_res_to_fire[i] == 56) {    //check if it's a eight
                    dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_8, 14700, true);
                    sleep_ms(700);     // wait 700 msec
                }
                else if (audio_res_to_fire[i] == 57) {    //check if it's a nine
                    dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_9, 22050, true);
                    sleep_ms(700);     // wait 700 msec
                }
                else {    //check if it's the end of the array
                    dma_channel_configure(sound_channel, &c0, &spi_get_hw(SPI_PORT)->dr, Sound_Ohms, 23888, true);
                    sleep_ms(700);     // wait 700 msec
                }
            }
            // Reset audio thread to wait for next time the button is pressed
            audio = 0;
            sprintf(audio_text, "%s", "Audio not selected.");
        }
        PT_YIELD_usec(30000);
    }
    // Indicate thread end
    PT_END(pt);
}

////////////////////////// Start core 1 //////////////////////////
void core1_main(){
    // add threads
    pt_add_thread(protothread_audio);
    // start scheduler
    pt_schedule_start;
}

int main() {
    ////////////////////// Initialize stdio /////////////////////////////
    stdio_init_all();

    /////////////////////// Start core 1 //////////////////////////////////
    multicore_reset_core1();
    multicore_launch_core1(&core1_main);

    /////////////////// Initialise ADC for voltage divider ////////////////////
    adc_init();
    adc_gpio_init(ADC_0);
    adc_select_input(0);

    //////////////////////// Initialize TFT Display ////////////////////////////
    tft_init_hw();      //Initialize the hardware for the TFT
    tft_begin();    //Initialize the TFT
    tft_setRotation(45);    //Set TFT rotation
    tft_fillScreen(ILI9340_BLACK);      //Fill the entire screen with black color
    
    ///////////////////// PMW configurations for ISR ///////////////////////////////
    slice_num = pwm_gpio_to_slice_num(0);   // Find out which PWM slice is connected to GPIO 0
    pwm_clear_irq(slice_num);   // Mask our slice's IRQ output into the PWM block's single interrupt line, and register our interrupt handler
    pwm_set_irq_enabled(slice_num, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, ISR);
    irq_set_enabled(PWM_IRQ_WRAP, true);
    pwm_set_wrap(slice_num, WRAPVAL) ;      // This section configures the period of the PWM signals
    pwm_set_clkdiv(slice_num, CLKDIV) ;
    pwm_set_chan_level(slice_num, PWM_CHAN_B, 0);   // This sets duty cycle
    pwm_set_mask_enabled((1u << slice_num));    // Start the channel

    //////////////////////////// Initialize DAC ///////////////////////////////////////
    spi_init(SPI_PORT, 20000000);       // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);       // Format (channel, data bits per transfer, polarity, phase, order)

    // Map SPI signals to GPIO ports
    gpio_set_function(MISO, GPIO_FUNC_SPI);
    gpio_set_function(DAC_SCK, GPIO_FUNC_SPI);
    gpio_set_function(DAC_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(DAC_CS, GPIO_FUNC_SPI);

    // Map LDAC (MISO) pin to GPIO port, hold it low (could alternatively tie to GND)
    gpio_init(LDAC) ;
    gpio_set_dir(LDAC, GPIO_OUT) ;
    gpio_put(LDAC, 0) ;
    
    ///////////////////// Initialize buttons ///////////////////////
    gpio_init(BUTTON_3);    // Button 3 initialize
    gpio_set_dir(BUTTON_3, GPIO_IN);
    gpio_pull_up(BUTTON_3);
    gpio_init(BUTTON_4);    // Button 4 initialize
    gpio_set_dir(BUTTON_4, GPIO_IN);
    gpio_pull_up(BUTTON_4);
    gpio_init(BUTTON_5);    // Button 5 initialize
    gpio_set_dir(BUTTON_5, GPIO_IN);
    gpio_pull_up(BUTTON_5);
    gpio_init(BUTTON_6);    // Button 6 initialize
    gpio_set_dir(BUTTON_6, GPIO_IN);
    gpio_pull_up(BUTTON_6);

    ////////////////////////// Initialize LEDs ///////////////////////////
    // Initialise LEDs for lamp
	gpio_init(LED_PIN);
	gpio_set_dir(LED_PIN, GPIO_OUT);
	gpio_put(LED_PIN, false); /* Important to start low to tell the LEDs that it's time for new data */
    
	int32_t led = 0; uint8_t led_dir = 1; uint8_t dim_value = 1; uint8_t dim_dir = 1;
    set_all(204,255,255);
	send_led_data(LED_PIN);

    // Initialise LEDs for border
    gpio_init(LED_PIN_BORDER);
	gpio_set_dir(LED_PIN_BORDER, GPIO_OUT);
	gpio_put(LED_PIN_BORDER, false); /* Important to start low to tell the LEDs that it's time for new data */
	
    set_all(red, green, blue);
	send_led_data(LED_PIN_BORDER);
    
    //////////////////////////// Initialize Audio DMA Channels ///////////////////////////
    // Setup DMA Channel 0 (any sound to fire)
    c0 = dma_channel_get_default_config(sound_channel);  // Default configs
    channel_config_set_transfer_data_size(&c0, DMA_SIZE_16);            // 16-bit txfers
    channel_config_set_read_increment(&c0, true);                       // yes read incrementing
    channel_config_set_write_increment(&c0, false);                     // no write incrementing
    // (X/Y)*sys_clk, where X is the first 16 bytes and Y is the second
    // sys_clk is 125 MHz unless changed in code. Configured to ~44 kHz
    dma_timer_set_fraction(0, 0x0017, 0xffff) ;
    // 0x3b means timer0 (see SDK manual)
    channel_config_set_dreq(&c0, 0x3b);                                 // DREQ paced by timer 0

    dma_channel_configure(
        sound_channel,                  // Channel to be configured
        &c0,                        // The configuration we just created
        &spi_get_hw(SPI_PORT)->dr,  // write address (SPI data register)
        Sound_Intro,                   // The initial read address
        53288,            // Number of transfers
        false                       // Don't start immediately.
    );

    /////////////////////////////// Start core 0 /////////////////////////////////
    pt_add_thread(protothread_display);
    pt_add_thread(protothread_button);
    pt_add_thread(protothread_leds);
    pt_schedule_start;
}