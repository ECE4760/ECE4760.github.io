
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
// Include PICO libraries
#include "pico/stdlib.h"
#include "pico/multicore.h"
// Include hardware libraries
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
// Include custom libraries
#include "vga16_graphics_v2.h"
#include "mpu6050.h"
#include "pt_cornell_rp2040_v1_4.h"
#include "bno08x.h"
#include "dfPlayerDriver.h"

BNO08x IMU;
DfPlayerDriver dfp(uart1, 8, 9);

// Pound define values
#define PI 3.14159265358979328462643383279028841
#define TWO_PI (2.0 * PI)
#define PI_OVER_FOUR PI/4.0
#define PI_OVER_SIX PI/6.0
#define PI_OVER_THREE PI/3.0
#define PI_OVER_TWO PI/2.0
#define THREE_PI_OVER_FOUR (3.0 * PI)/4.0

// Some macros for max/min/abs
#define min(a,b) ((a<b) ? a:b)
#define max(a,b) ((a<b) ? b:a)
#define abs(a) ((a>0) ? a:-a)

float hit_thresh = 0;
float margin = 0.05;

//One drumstick
float yaw = 0.0f;
float pitch = 0.0f;
float roll = 0.0f;

float old_yaw = 0.0f;
float old_pitch = 0.0f;
float old_roll = 0.0f;

int state = 0;
int state2 = 0;
float og_yaw = 0.0f;
float og_pitch = 0.0f;
float og_roll = 0.0f;
int drums = 0;

float yaw_dx = 0;
float roll_dx = 0;
float pitch_dx = 0;

//external button
int button_pressed = 0;
#define BUTTON_PIN 12

int folder = 1;
int folder_max = 4;
int current_state;
int last_state;

float x;
float y;
float volume[31];

    ////////////////////////////////////////////////////////////////////////
    ///////////////////////////// FIRST DRUMSTICK //////////////////////////
    ////////////////////////////////////////////////////////////////////////

static PT_THREAD (protothread_imu(struct pt *pt)){
    PT_BEGIN(pt) ;
    PT_INTERVAL_INIT();

    while (true) {

        //reading data from the IMU
        //recenters all value at a specified location that can be recalibrated
        if (IMU.getSensorEvent()) {
            if (IMU.getSensorEventID() == SENSOR_REPORTID_ROTATION_VECTOR) {
                yaw = IMU.getYaw() - og_yaw;
                if (yaw > PI) yaw -= TWO_PI;
                else if (yaw < -PI) yaw += TWO_PI;
                roll = IMU.getRoll() - og_roll;  
                if (roll > PI) roll -= TWO_PI;
                else if (roll < -PI) roll += TWO_PI;
                pitch = IMU.getPitch() - og_pitch;  
                if (pitch > PI) pitch -= TWO_PI;
                else if (pitch < -PI) pitch += TWO_PI; 
            }
        }
        
        //scaling roll pitch and yaw speed for easier use
        roll_dx = 150*(roll-old_roll);
        pitch_dx = 150*(pitch-old_pitch);
        yaw_dx = 150*(yaw-old_yaw);

        //remembers the yaw of the previous scan
        old_yaw = yaw;
        old_roll = roll;
        old_pitch = pitch;
        PT_YIELD(pt);  

    }
    PT_END(pt);
    
}

//determins the radial location of different soundfiles for each folder
static PT_THREAD (protothread_drums(struct pt *pt)){
    PT_BEGIN(pt) ;
    PT_INTERVAL_INIT();

    while (true) {

        //drumkit and meme kit
        if(folder==1 || folder==3){
            if (yaw >= -PI_OVER_TWO && yaw < -PI_OVER_THREE) drums = 6;
            else if (yaw >= -PI_OVER_THREE && yaw < -PI_OVER_SIX) drums = 5;
            else if (yaw >= -PI_OVER_SIX && yaw < 0) drums = 4;
            else if (yaw >= 0 && yaw < PI_OVER_SIX) drums = 3;
            else if (yaw >= PI_OVER_SIX && yaw < PI_OVER_THREE) drums = 2;
            else if (yaw >= PI_OVER_THREE && yaw < PI_OVER_TWO) drums = 1;
            else drums = 0;
        }

        //piano scale
        else if(folder==2){
            if(yaw >= -PI && yaw < -THREE_PI_OVER_FOUR) drums = 6;
            else if (yaw >= -THREE_PI_OVER_FOUR && yaw < -PI_OVER_TWO) drums = 5;
            else if (yaw >= -PI_OVER_TWO && yaw < -PI_OVER_FOUR) drums = 4;
            else if (yaw >= -PI_OVER_FOUR && yaw < 0) drums = 3;
            else if (yaw >= 0 && yaw < PI_OVER_FOUR) drums = 2;
            else if (yaw >= PI_OVER_FOUR && yaw < PI_OVER_TWO) drums = 1;
            else if (yaw >= PI_OVER_TWO && yaw < THREE_PI_OVER_FOUR) drums = 8;
            else if (yaw >= THREE_PI_OVER_FOUR && yaw <= PI) drums = 7;
        }

        //drum solo
        else if(folder==4){
            drums = 1;
        }

        PT_YIELD(pt);

    }
    PT_END(pt);
    
}

// Hit Detection Thread
static PT_THREAD (protothread_hit(struct pt *pt))
{
    PT_BEGIN(pt) ;
    PT_INTERVAL_INIT();

    while (true) {
        if (state == 0 && pitch < hit_thresh && drums != 0){ 
            
            //inverse sigmoid sound mapping
            if(pitch_dx < volume[30]) {dfp.volume(30);}
            else if(pitch_dx < volume[29]) {dfp.volume(29);}
            else if(pitch_dx < volume[28]) {dfp.volume(28);}
            else if(pitch_dx < volume[27]) {dfp.volume(27);}
            else if(pitch_dx < volume[26]) {dfp.volume(26);}
            else if(pitch_dx < volume[25]) {dfp.volume(25);}
            else if(pitch_dx < volume[24]) {dfp.volume(24);}
            else if(pitch_dx < volume[23]) {dfp.volume(23);}
            else if(pitch_dx < volume[22]) {dfp.volume(22);}
            else if(pitch_dx < volume[21]) {dfp.volume(21);}
            else if(pitch_dx < volume[20]) {dfp.volume(20);}
            else if(pitch_dx < volume[19]) {dfp.volume(19);}
            else if(pitch_dx < volume[18]) {dfp.volume(18);}
            else if(pitch_dx < volume[17]) {dfp.volume(17);}
            else if(pitch_dx < volume[16]) {dfp.volume(16);}
            else if(pitch_dx < volume[15]) {dfp.volume(15);}
            else if(pitch_dx < volume[14]) {dfp.volume(14);}
            else if(pitch_dx < volume[13]) {dfp.volume(13);}
            else if(pitch_dx < volume[12]) {dfp.volume(12);}
            else if(pitch_dx < volume[11]) {dfp.volume(11);}
            else if(pitch_dx < volume[10]) {dfp.volume(10);}
            else if(pitch_dx < volume[9]) {dfp.volume(9);}
            else if(pitch_dx < volume[8]) {dfp.volume(8);}
            else if(pitch_dx < volume[7]) {dfp.volume(7);}
            else if(pitch_dx < volume[6]) {dfp.volume(6);}
            else if(pitch_dx < volume[5]) {dfp.volume(5);}
            else if(pitch_dx < volume[4]) {dfp.volume(4);}
            else if(pitch_dx < volume[3]) {dfp.volume(3);}
            else if(pitch_dx < volume[2]) {dfp.volume(2);}
            else {dfp.volume(1);}

            if(folder==4){
                dfp.volume(30);
            }
            
            //playing the specified sound track
            dfp.playFolderTrack(folder, drums);  
            state = 1;
        }
        else if (state == 1 && pitch > hit_thresh + margin){
            state = 0;
        }
        
        PT_YIELD(pt);
    }

    PT_END(pt) ;
}

//plays the transition audio when switching sound folders
void play_transition(){
    dfp.volume(20);
    dfp.playFolderTrack(5, folder);
}

// protothread for motion activated music select
static PT_THREAD(protothread_music_select(struct pt *pt)){
    PT_BEGIN(pt);
    PT_INTERVAL_INIT();

    while(true){
        // If Not Pressed
           if(state2 == 0){
               // If button is pressed
               if ((roll > THREE_PI_OVER_FOUR)||(roll < -THREE_PI_OVER_FOUR)){
                    state2 = 1;
               }
           }

           // If Maybe Pressed
           else if(state2 == 1){
               if ((roll > THREE_PI_OVER_FOUR)||(roll < -THREE_PI_OVER_FOUR)){
                   state2 = 2;
                   folder +=1;
                    if(folder > folder_max){folder = 1;}
                    play_transition();
               }
               else{
                   state2 = 0;
               }
           }

           // If Pressed
           else if(state2 == 2){
               if ((roll > THREE_PI_OVER_FOUR)||(roll < -THREE_PI_OVER_FOUR)){
                    state2 = 2;
               }
               else{
                   state2 = 3;
               }
           }

           // If Maybe Not Pressed
           else{
               if ((roll > THREE_PI_OVER_FOUR)||(roll < -THREE_PI_OVER_FOUR)){
                   state2 = 2;
               }
               else{
                   state2 = 0;
               }
           }
        PT_YIELD(pt);

    }
    PT_END(pt) ;
}

// protothread for the external button sequence
static PT_THREAD(protothread_button(struct pt *pt)){
    PT_BEGIN(pt) ;
    // waiting for the button to get pushed
    current_state = gpio_get(BUTTON_PIN);
    last_state = current_state;
    while (true) {
        current_state = gpio_get(BUTTON_PIN);
        // Trigger on *any* change (HIGH->LOW OR LOW->HIGH)
        if (current_state != last_state) {
            // Button transitioned
            og_yaw = IMU.getYaw();
            og_pitch = IMU.getPitch();
        }

        last_state = current_state;
        PT_YIELD(pt);
    }
    PT_END(pt) ;
}



int main() {

    set_sys_clock_khz(150000, true) ;
    stdio_init_all();

    ////////////////////////////////////////////////////////////////////////
    ///////////////////////// IMU CONFIGURATION ////////////////////////////

    i2c_inst_t* i2c_port0 = i2c0; 
    i2c_init(i2c_port0, 400 * 1000); 
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C) ;
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C) ;
    gpio_pull_up(4);
    gpio_pull_up(5);

    //makes sure the IMU and RP2040 set up I2C communication
    while (!IMU.begin(0x4A, i2c_port0)) {
        printf("BNO08x not detected at default I2C address. Check wiring.\n");
        sleep_ms(1000);
    }

    IMU.enableRotationVector();

    //grabs the yaw measurment to reset it as the origin
    while(og_yaw == 0.0){
        if (IMU.getSensorEvent()) {
            if (IMU.getSensorEventID() == SENSOR_REPORTID_ROTATION_VECTOR) {
                og_yaw = IMU.getYaw();
            }
        }
    }

    //grabs the pitch measurment to reset it as the origin
    while(og_pitch == 0.0){
        if (IMU.getSensorEvent()) {
            if (IMU.getSensorEventID() == SENSOR_REPORTID_ROTATION_VECTOR) {
                og_pitch = IMU.getPitch();
            }
        }
    }

    //grabs the roll measurment to reset it as the origin
    while(og_roll == 0.0){
        if (IMU.getSensorEvent()) {
            if (IMU.getSensorEventID() == SENSOR_REPORTID_ROTATION_VECTOR) {
                og_roll = IMU.getRoll();
            }
        }
    }

    ////////////////////////////////////////////////////////////////////////
    ///////////////////////// DFP CONFIGURATION ////////////////////////////

    gpio_set_function(8, GPIO_FUNC_UART);
    gpio_set_function(9, GPIO_FUNC_UART);
    uart_init(uart1, 9600);

    dfp.reset();
    sleep_ms(1500);

    dfp.volume(30);
    sleep_ms(200);

    //initialize external button for recalibration of origin
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);

    //mapping the inverse sigmoid function into an array for sound mapping
    for(int i = 0; i <=30; i++){
        x = (20.0 / (1.0 + exp(0.25 * (i - 15.0)))) - 20.0;
        volume[i] = x;
    }

    ////////////////////////////////////////////////////////////////////////
    ///////////////////////////// ROCK AND ROLL ////////////////////////////
    ////////////////////////////////////////////////////////////////////////

    //start core 0
    pt_add_thread(protothread_imu);
    pt_add_thread(protothread_hit);
    pt_add_thread(protothread_drums);
    pt_add_thread(protothread_button);
    pt_add_thread(protothread_music_select) ;
    pt_schedule_start ;

}

