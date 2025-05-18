/**
 * Cat Robot Software Version 2
 * 
 * Charlie Wright (caw352@cornell.edu)
 * Jeff Wilcox (jtw88@cornell.edu)
 * Lauren Lee (lsl88@cornell.edu)
 * 
 * This project uses IR receivers, DC motors, and servo motors to detect the direction of optical signals from targets and
 * move towards them. An LCD screen will be used to emulate the eyes of a cat.
 * 
 */

/* LIBRARY IMPORTS */

// Standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

// PICO Libraries
#include "pico/stdlib.h"
#include "pico/multicore.h"

// PWM used for driving the DC and servo motors
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "servo.h"

// IRQ used for detecting optical signals
#include "hardware/irq.h"

//PIO for LCD SPI interface
#include "hardware/pio.h"

//LCD libraries
#include "TFTmaster.h"
// #include "tft_gfx.h" //we dont have!

/* May need ADC to convert optical signals from analog to digital,
but should not be necessary based on IR receiver schematic */
#include "hardware/adc.h"

// Include Custom Libraries, protothreads
#include "pt_cornell_rp2040_v1_3.h"


/* VARIABLES */

// IR Detector Variables
//*  - GPIO 14 () ---> OUT
#define irqPin 14
static int flag = 0;


// DC Motor Variables
/**
 * RELEVANT HARDWARE CONNECTIONS
 *  Motor Controller for Side
 *  - GPIO 11 () ---> AIN1
 *  - GPIO 12 () ---> AIN2
 *  - MCU GND (Pin 18) ---> GND
 *  - GPIO 13 () ---> SLP
 */

 #define AIN1 11
 #define AIN2 12
 #define SLP 13

// Servo Motor Variables
#define frontLeftServoPin 1
#define frontRightServoPin 6
#define backLeftServoPin 4
#define backRightServoPin 7
int currentMillis = 1450;
bool direction = true;

// LCD Screen Variables
/**
 * We should use landscape mode with the VGA which requires setting rotation mode to 1
 * 
 */
 /**
 * RELEVANT HARDWARE CONNECTIONS
 *  LCD GPIOs specified in TFTMaster.h
 *  - GPIO 18 ---> TFT Chip Select
 *  - GPIO 19 ---> TFT MOSI
 *  - GPIO 17 ---> TFT SCK
 *  - GPIO 16 ---> TFT D/C
 *  - 3V3 ---> TFT VIN
 */
 // string buffer
char buffer[60];
//eye and pupil dimensions
const int lcd_w = 320;
const int lcd_h = 240;
const int centx = lcd_w / 2;
const int centy = lcd_h / 2;
#define EYE_COLOR ILI9340_GREEN
#define BG_COLOR ILI9340_BLACK
#define PUP_COLOR ILI9340_BLACK
const int distbtw = 160;
#define EYE_LEFT_X_DEF centx-(distbtw/2)
#define EYE_RIGHT_X_DEF centx+(distbtw/2)
#define EYE_LEFT_Y_DEF centy
#define EYE_RIGHT_Y_DEF centy

//eye reference points
#define EYE_HEIGHT_DEF 110
#define PUPIL_HEIGHT_DEF 50
static int eyeh = EYE_HEIGHT_DEF;
static int eyew = 110;
static int pupilh = PUPIL_HEIGHT_DEF;
static int pupilw = 50;
static int eye_corner_rad = 10;

static int eye_midpt_x = centx;
static int eye_midpt_y = centy;
static int pupil_midpt_x = centx;
static int pupil_midpt_y = centy;

static int eye_xl;
static int eye_yl;
static int eye_xr;
static int eye_yr;

static int pup_xl = EYE_LEFT_X_DEF;
static int pup_yl = EYE_LEFT_Y_DEF;
static int pup_xr = EYE_RIGHT_X_DEF;
static int pup_yr = EYE_RIGHT_Y_DEF;

//blinking variables
static int blinking = 0;
static int opening = 0;
static int blink_time = 0;
static int blink_time_limit = 1000000;//1000000-3000000 is a good range

/* FUNCTIONS */

//converts angle to duty cycles for front legs
//float angle is 0-180 degrees
void serve_front(float angle){
    float pwm_in = 1900*(angle/180)+500;
    setMillis(frontLeftServoPin, 2900-pwm_in);
    sleep_ms(150);
    setMillis(frontRightServoPin, pwm_in+53);
}

//converts angle to duty cycles for rear legs
void serve_rear(float angle){
    //serve!!
    float pwm_in = 1900*(angle/180)+500;
    setMillis(backRightServoPin, pwm_in);
    setMillis(backLeftServoPin, 2900-pwm_in);
}

//servo testing functions
void servoTest(){
    // Center/straight down is 1450
    // One horizontal point is 500, the other is 2400
    currentMillis += (direction)?200:-200;

    if (currentMillis >= 1650){
        direction = false;
        currentMillis = 1650;
        // gpio_put(25,true);
    }
    if (currentMillis <= 1250){
        direction = true;
        currentMillis = 1250;
    }
    setMillis(backLeftServoPin, currentMillis);
    setMillis(backRightServoPin, 2900-currentMillis);
}

//update functions after eye positions or sizes are changed
void update_eye_pos(){
    eye_xl = eye_midpt_x - (distbtw/2) - (eyew/2);
    eye_xr = eye_midpt_x + (distbtw/2) - (eyew/2);
    eye_yl = eye_midpt_y - (eyeh/2) - 10;
    eye_yr = eye_midpt_y - (eyeh/2) - 10;
}

void update_pupil_pos(){
    pup_xl = pupil_midpt_x - (distbtw/2) - (pupilw/2);
    pup_xr = pupil_midpt_x + (distbtw/2) - (pupilw/2);
    pup_yl = pupil_midpt_y - (pupilh/2);
    pup_yr = pupil_midpt_y - (pupilh/2);
}

//Creates eye shape at specified sizes
void draw_eyes(){
    tft_fillRoundRect(eye_xr, eye_yr, eyew, eyeh, eye_corner_rad, EYE_COLOR);
    tft_fillRoundRect(eye_xl, eye_yl, eyew, eyeh, eye_corner_rad, EYE_COLOR);
    tft_drawRoundRect(eye_xr, eye_yr, eyew, eyeh, eye_corner_rad, ILI9340_WHITE);
    tft_drawRoundRect(eye_xl, eye_yl, eyew, eyeh, eye_corner_rad, ILI9340_WHITE);
    tft_fillRoundRect(pup_xr, pup_yr, pupilw, pupilh, eye_corner_rad, PUP_COLOR);
    tft_fillRoundRect(pup_xl, pup_yl, pupilw, pupilh, eye_corner_rad, PUP_COLOR);
}


void blink(int speed){
    //LOOKS WEIRD AND MIGHT MESS W TIMING
    if (!blinking){
        blinking = 1;
        opening = 0;}
    if (!opening){

        tft_fillRoundRect(eye_xr, eye_yr - 3, eyew, eyeh, eye_corner_rad, BG_COLOR);
        tft_fillRoundRect(eye_xl, eye_yl - 3, eyew, eyeh, eye_corner_rad, BG_COLOR);
        opening = 1;

    } else {
        draw_eyes();
        opening = 0;
        blinking = 0;
    }
}

void dialate_pupils(){

    while (pupilh < 1.6*PUPIL_HEIGHT_DEF){
        pupilh += 4 ;
        pupilw += 4 ;

        pupil_midpt_x -= 1;
        pupil_midpt_y -= 1;
        update_pupil_pos();
        tft_fillRoundRect(pup_xr, pup_yr, pupilw, pupilh, eye_corner_rad, PUP_COLOR);
        tft_fillRoundRect(pup_xl, pup_yl, pupilw, pupilh, eye_corner_rad, PUP_COLOR);
        sleep_ms(10);
    }
}

//secquence for pouncing action, set timings prevent other threads from running
void pounce_seq(){
    //dialate pupils
    dialate_pupils();
    //crouch down
    serve_front(135);
    serve_rear(90);
    sleep_ms(3000);
    //stand back up
    serve_front(100);
    serve_rear(80);
    sleep_ms(1000);

    //drive forward for 3 seconds
    gpio_put(AIN1, true);
    gpio_put(AIN2,false);
    sleep_ms(3000);
    gpio_put(AIN1, false);
    gpio_put(AIN2,false);
    sleep_ms(1000);

    //reset eyes
    pupil_midpt_x = centx;
    pupil_midpt_y = centy;
    pupilh = PUPIL_HEIGHT_DEF;
    pupilw = PUPIL_HEIGHT_DEF;
    update_pupil_pos();
    draw_eyes();

}

/* INTERRUPTS*/

void IR_interrupt()
{
    //set flag, prevents resetting the sequence if signal is detected mid-pounce
    flag = 1;

}

/* PROTOTHREADS */
static PT_THREAD (pt_anim(struct pt *pt)) //LCD Animation
{
    PT_BEGIN(pt);
    
    draw_eyes();

      while(1) {
        //smiley
        // tft_drawPixel(centx-3, centy-2, ILI9340_CYAN);
        // tft_drawPixel(centx+3, centy-2, ILI9340_CYAN);
        // tft_drawPixel(centx-3, centy+1, ILI9340_CYAN);
        // tft_drawPixel(centx+2, centy+2, ILI9340_CYAN);
        // tft_drawPixel(centx+1, centy+2, ILI9340_CYAN);
        // tft_drawPixel(centx, centy+2, ILI9340_CYAN);
        // tft_drawPixel(centx+1, centy+2, ILI9340_CYAN);
        // tft_drawPixel(centx+2, centy+2, ILI9340_CYAN);
        // tft_drawPixel(centx+3, centy+1, ILI9340_CYAN);

        // increment blink timer, open/close eye on an interval     
        if (blink_time == blink_time_limit){
            blink(0);
            blink_time = blink_time + 1; 
        }else if (blink_time >= blink_time_limit+3000){
            blink(0);
            blink_time = 0;
        }else{ 
            blink_time = blink_time + 1; 
        }

        PT_YIELD(pt);
      } // END WHILE(1)
  PT_END(pt);
} // animation thread

static PT_THREAD(pt_pounce(struct pt *pt)){
    PT_BEGIN(pt);
    while(1){
        if(flag){
            gpio_put(25,true);
            pounce_seq();
            gpio_put(25,false);
            flag = 0;
        }
        PT_YIELD(pt);
    }
    PT_END(pt);
}

int main()
{
    stdio_init_all();

    // // Servo Initialization
    gpio_init(frontLeftServoPin);
    gpio_set_dir(frontLeftServoPin, true);
    gpio_init(backLeftServoPin);
    gpio_set_dir(backLeftServoPin, true);
    gpio_init(frontRightServoPin);
    gpio_set_dir(frontRightServoPin, true);
    gpio_init(backRightServoPin);
    gpio_set_dir(backRightServoPin, true);
    setServo(frontLeftServoPin, currentMillis);
    setServo(backLeftServoPin, currentMillis);
    setServo(frontRightServoPin, currentMillis);
    setServo(backRightServoPin, currentMillis);
    serve_front(110);
    serve_rear(80);

    //initial eyes drawn
    draw_eyes();

    gpio_init(25);
    gpio_set_dir(25, true);

    //configure IR pins
    gpio_init(irqPin);
    gpio_set_dir(irqPin, GPIO_IN);
    gpio_pull_up(irqPin);
    // gpio_set_irq_enabled(irqPin, GPIO_IRQ_EDGE_FALL, true);
    //enable interrupt with callback version
    gpio_set_irq_enabled_with_callback(irqPin, GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE, true, &IR_interrupt);


    // Configure pins (DC Motors)
    gpio_init(11);
    gpio_set_dir(11,true);
    gpio_init(12);
    gpio_set_dir(12,true);
    gpio_init(13);
    gpio_set_dir(13,true);
    gpio_put(13,true);

    // init LCD
    tft_init_hw();
    tft_begin();
    tft_fillScreen(BG_COLOR);
    update_eye_pos();
    update_pupil_pos();
    //240x320 vertical display
    tft_setRotation(1); // Use tft_setRotation(1) for 320x240

    //Add threads
    pt_add_thread(pt_pounce);
    pt_add_thread(pt_anim);
    pt_schedule_start;

    //Servo initializing
}