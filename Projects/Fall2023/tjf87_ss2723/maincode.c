<!DOCTYPE html PUBLIC>
<html>
    <head>
        <title>
            ECE 4760 Game Code
        </title>
    </head>
    <body>
        <pre style = "word-wrap: break-word; white-space: pre-wrap;">
/**
*  - GPIO 26 ---> Adafruit SDA
*  - GPIO 27 ---> Adafruit SCL
*  - 3.3v ---> Adafruit VCC
    I2C Chan 2
*/

// Include vga library
#include "vga_graphics.h"
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
// Include Pico libraries
#include "pico/stdlib.h"
#include "pico/divider.h"
#include "pico/multicore.h"
// Include hardware libraries
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "pico/multicore.h"
#include "hardware/sync.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "adafruit.h"
// Include protothreads
#include "pt_cornell_rp2040_v1.h"


int pos[2]; // The position from the ADPS
int playShootSound = 0;
int playHitSound = 0;
int playDeathSound=0;
//======== Fixed Point Macros ========================================
typedef signed int fix15 ;
#define multfix15(a,b) ((fix15)((((signed long long)(a))*((signed long long)(b)))>>15))
#define float2fix15(a) ((fix15)((a)*32768.0)) // 2^15
#define fix2float15(a) ((float)(a)/32768.0)
#define absfix15(a) abs(a) 
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a,b) (fix15)(div_s64s64( (((signed long long)(a)) << 15), ((signed long long)(b))))
#define FRAME_RATE  33000


//======== DDS Stuff ========================================
//DDS parameters
#define two32 4294967296.0 // 2^32 
#define Fs 40000
// the DDS units - core 1
volatile unsigned int phase_accum_main_1;
volatile unsigned int phase_incr_main_1 = (800.0*two32)/Fs ;
// the DDS units - core 2
volatile unsigned int phase_accum_main_0;
volatile unsigned int phase_incr_main_0 = (400.0*two32)/Fs ;

// DDS sine table
#define sine_table_size 256
fix15 sin_table[sine_table_size] ;

// Values output to DAC
int DAC_output_0 ;
int DAC_output_1 ;

// Amplitude modulation parameters and variables
fix15 max_amplitude = int2fix15(1) ;
fix15 attack_inc ;
fix15 decay_inc ;
fix15 current_amplitude_0 = 0 ;
fix15 current_amplitude_1 = 0 ;
#define ATTACK_TIME             200
#define DECAY_TIME              200
#define SUSTAIN_TIME            10000
#define BEEP_DURATION           10400
#define BEEP_REPEAT_INTERVAL    40000

//SPI data
uint16_t DAC_data_1 ; // output value
uint16_t DAC_data_0 ; // output value

//DAC parameters
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

//SPI configurations
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define LDAC     8
#define SPI_PORT spi0
#define LED      25

// Two variables to store core number
volatile int corenum_0  ;
volatile int corenum_1  ;

// Global counter for spinlock experimenting
volatile int global_counter = 0 ;

// Counter spinlock
int spinlock_num_count ;
spin_lock_t *spinlock_count ;

// State machine variables
volatile unsigned int STATE_0 = 0 ;
volatile unsigned int count_0 = 0 ;
volatile unsigned int STATE_1 = 0 ;
volatile unsigned int count_1 = 0 ;

volatile int play = 1;

int game_start = 0;
int score = 0;
int pause = 0;
int lives = 3;
int NUM_ENEMIES = 10;
int move_step = -1;
int ammo = 0;
int shoot_bullet = 0;
int last_ship_pos = 0;
int died = 0;
int win = 0;
static float lowpass =0;

int lastb10;
int lastb11;
int lastb12;
int lastb13;
int lastb14;
int lastb15;

// Struct Definitions for the objects
typedef struct object_reg{
    int object_array[11][11];
    int top_left[2];
    int top_right[2];
    int bot_left[2];
    int bot_right[2];
    int draw;
    int x;
    int y;
    int shot;
}object_reg;

typedef struct object_big{
    int object_array[18][17];
    int top_left[2];
    int top_right[2];
    int bot_left[2];
    int bot_right[2];
    int draw;
    int x;
    int y;
    int shot;
}object_big;


//array definition for regular objects
typedef struct object_array_struct{
    object_reg objects[10];
}object_array;

//array definitions for big objects
typedef struct object_array_struct_big{
    object_big objects[10];
}object_array_b;

//object strutcture of the ship.
object_reg user_ship = {
    .object_array = {{0,0,0,0,0,1,0,0,0,0,0}, 
                {0,0,0,0,0,1,0,0,0,0,0},
                {0,0,0,0,0,1,0,0,0,0,0},
                {0,0,3,0,1,1,1,0,3,0,0},
                {0,0,1,0,1,1,1,0,1,0,0},
                {0,0,1,2,1,3,1,2,1,0,0},
                {3,0,2,1,3,3,3,1,2,0,3},
                {1,0,1,1,3,1,3,1,1,0,1},
                {1,1,1,1,1,1,1,1,1,1,1},
                {1,1,0,3,3,1,3,3,0,1,1},
                {1,0,0,0,0,1,0,0,0,0,1}},
    .top_left = {200,300},
    .top_right = {211,300},
    .bot_left = {200,289},
    .bot_right = {211, 289},
    .draw = 1,
    .x = 11,
    .y = 11,
    .shot = 0
};
//object structure of the player bullet
object_reg bullet = {
    .object_array = {{0,0,0,0,0,0,0,0,0,0,0},
                     {0,0,0,0,0,1,0,0,0,0,0},
                     {0,0,0,0,1,1,1,0,0,0,0},
                     {0,0,0,1,1,1,1,1,0,0,0},
                     {0,0,1,2,2,2,2,2,1,0,0},
                     {0,1,1,2,2,2,2,2,1,1,0},
                     {1,0,0,2,2,2,2,2,0,0,1},
                     {0,0,0,0,0,0,0,0,0,0,0},
                     {0,0,0,0,0,0,0,0,0,0,0},
                     {0,0,0,0,0,0,0,0,0,0,0},
                     {0,0,0,0,0,0,0,0,0,0,0}},

    .top_left = {200,400},
    .top_right = {211,400},
    .bot_left = {200,389},
    .bot_right = {211, 389},
    .draw = 0,
    .x = 11,
    .y = 11,
    .shot = 0
};
//object structure of the enemy bullet
object_reg enemy_bullet = {
   .object_array =  {{0,0,0,0,0,0,0,0,0,0,0},
                     {0,0,0,0,0,4,0,0,0,0,0},
                     {0,0,0,0,4,4,4,0,0,0,0},
                     {0,0,0,4,1,1,1,4,0,0,0},
                     {0,0,0,2,2,2,2,2,0,0,0},
                     {0,0,5,2,2,2,2,2,5,0,0},
                     {5,5,5,2,2,2,2,2,5,5,5},
                     {0,5,5,0,0,0,0,0,5,5,0},
                     {0,0,5,4,4,4,4,4,5,0,0},
                     {0,0,0,0,3,3,3,0,0,0,0},
                     {0,0,0,0,0,2,0,0,0,0,0}},
    .top_left = {0 ,10},
    .top_right = {10,10},
    .bot_left = {0,0},
    .bot_right = {10, 0},
    .draw = 0,
    .x = 11,
    .y = 11,
    .shot = 0 
};
//object structure of the "regular" enemies
object_reg enemy10 = {
    .object_array = {{0,0,0,0,0,0,0,0,0,0,0},
                     {0,0,0,0,0,0,0,0,0,0,0},
                     {0,0,0,0,4,4,4,0,0,0,0},
                     {0,0,2,0,4,2,4,0,2,0,0},
                     {0,0,4,0,4,2,4,0,4,0,0},
                     {0,4,4,4,3,3,3,4,4,4,0},
                     {0,4,4,4,3,2,3,4,4,4,0},
                     {0,0,0,0,3,3,3,0,0,0,0},
                     {0,0,0,0,4,2,4,0,0,0,0},
                     {0,0,0,0,0,4,0,0,0,0,0},
                     {0,0,0,0,0,0,0,0,0,0,0}},

    .top_left = {200,200},
    .top_right = {211,200},
    .bot_left = {200,189},
    .bot_right = {211, 189},
    .draw = 1,
    .x =11,
    .y = 11,
    .shot = 0
};
//object structure of the "big" enemies
object_big bigEnemy = {
    .object_array ={
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 5, 0, 5, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 5, 0, 5, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 5, 5, 3, 3, 5, 3, 3, 5, 5, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 5, 3, 3, 5, 3, 3, 5, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 5, 7, 7, 5, 7, 7, 5, 0, 0, 0, 0, 0},
            {0, 0, 0, 5, 5, 5, 7, 7, 7, 7, 7, 5, 5, 5, 0, 0, 0},
            {0, 5, 5, 5, 5, 5, 7, 7, 7, 7, 7, 5, 5, 5, 5, 5, 0},
            {0, 0, 5, 5, 5, 5, 7, 7, 7, 7, 7, 5, 5, 5, 5, 0, 0},
            {0, 0, 5, 6, 5, 5, 7, 7, 7, 7, 7, 5, 5, 6, 5, 0, 0},
            {0, 5, 5, 6, 5, 0, 0, 6, 0, 6, 0, 0, 5, 6, 5, 5, 0},
            {0, 5, 6, 5, 5, 0, 0, 6, 0, 6, 0, 0, 5, 5, 6, 5, 0},
            {0, 5, 6, 6, 5, 0, 0, 0, 0, 0, 0, 0, 5, 6, 6, 5, 0},
            {0, 5, 6, 6, 5, 0, 0, 0, 0, 0, 0, 0, 5, 6, 6, 5, 0},
            {0, 5, 5, 5, 5, 0, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 0},
            {0, 0, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
        },
    .top_left = {200,200},
    .top_right = {211,200},
    .bot_left = {200,189},
    .bot_right = {211, 189},
    .draw = 1,
    .x = 17,
    .y = 18,
    .shot = 0
};

object_big bigBullet = {
     .object_array ={
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0},
            {0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0},
            {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
            {0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
        },
    .top_left = {200,200},
    .top_right = {211,200},
    .bot_left = {200,189},
    .bot_right = {211, 189},
    .draw = 0,
    .x = 17,
    .y = 18,
    .shot = 0

};

//object arrays
object_array enemy_objects;
object_array bullets_objects;
object_array_b enemy_objects2;
object_array enemy_bullets;
object_array_b big_enemy_bullets;

// Creates the boundary of the game area.
void drawBoundary(){
    drawVLine(50, 50, 300, WHITE);
    drawHLine(50, 50, 525, WHITE);
    drawVLine(575, 50, 300, WHITE);
    drawHLine(50, 350, 525, WHITE);
}

//clears the objects by drawing black over each pixel. Definittions for both "regular" and "big"
void clearObject(object_reg * t){
    for(int i = 0; i<t->x; i++ ){
        for(int k = 0; k<t->y; k++){
            if(t->object_array[i][k] != 0){
                drawPixel(k+ t->bot_left[0],i+ t->bot_left[1],BLACK);
            }
        }
    }
}
void clearObjectBig(object_big * t){
    for(int i = 0; i<t->x; i++ ){
        for(int k = 0; k<t->y; k++){
            if(t->object_array[i][k] != 0){
                drawPixel(k+ t->bot_left[0],i+ t->bot_left[1],BLACK);
            }
        }
    }
}


//Draws the objects in accordance to the object array in the object struct. Multiple definitions
// for "regular" and "small" objects
void drawObject(object_reg * t){
    char color;
    for(int i = 0; i<t->x; i++ ){
        for(int k = 0; k<t->y; k++){
            if(t->draw==1){
                switch(t->object_array[i][k]){
                    case 1: color = WHITE; break;
                    case 2: color = BLUE; break;
                    case 3: color = RED; break;
                    case 4: color = MAGENTA; break;
                    case 5: color = GREEN; break;
                    case 6: color = CYAN; break;
                    case 7: color = YELLOW; break;
                    default: color = BLACK; break;
                }
                drawPixel(k+ t->bot_left[0],i+ t->bot_left[1],color);
            }
        }
    }
}
void drawObjectBig(object_big * t){
    char color;
    for(int i = 0; i<t->x; i++ ){
        for(int k = 0; k<t->y; k++){
            if(t->draw==1){
                switch(t->object_array[i][k]){
                    case 1: color = WHITE; break;
                    case 2: color = BLUE; break;
                    case 3: color = RED; break;
                    case 4: color = MAGENTA; break;
                    case 5: color = GREEN; break;
                    case 6: color = CYAN; break;
                    case 7: color = YELLOW; break;
                    default: color = BLACK; break;
                }
                drawPixel(k+ t->bot_left[0],i+ t->bot_left[1],color);
            }
        }
    }
}

// Draws info such as health and scores and bullets to the VGA screen.
void drawInfo(int score, int lives, int time){
    fillRect(0,0,639,20, BLACK);
    char lives_m[10];
    char score_m[1000];
    char ammo_m[10];
    char time_m[100];
    setCursor(25,5);
    setTextSize(2);
    setTextColor(WHITE);
    sprintf(score_m, "%d", (score));
    writeString("Score: ");
    writeString(score_m);
    sprintf(lives_m, "%d", lives);
    writeString(" Lives: ");
    writeString(lives_m);
    int ammo_in = 5-ammo;
    sprintf(ammo_m, "%d", ammo_in);
    writeString(" Ammo: ");
    writeString(ammo_m);
    sprintf(time_m, "%d", time);
    writeString(" Spare Time: ");
    writeString(time_m);
}
void drawPause(){
    setCursor(260,200);
    setTextSize(4);
    writeString("PAUSED");
}

void drawDied(){
    setCursor(260,200);
    setTextSize(4);
    writeString("You Died");
}

void drawLost(){
    setCursor(260,200);
    setTextSize(4);
    writeString("You Lost");
}

void drawWin(){
    setCursor(260,200);
    setTextSize(4);
    writeString("You Win!");
}

void drawUnpause(){
    fillRect(240,200,400,50, BLACK);
}

void moveLeft(object_reg * t){
    clearObject(t);
    t->top_left[0] = t->top_left[0] + 10;
    t->top_right[0] = t->top_right[0] + 10;
    t->bot_right[0] = t->bot_right[0] + 10;
    t->bot_left[0] = t->bot_right[0] + 10;
    drawObject(t);
}
void moveRight(object_reg * t){
    clearObject(t);
    t->top_left[0] = t->top_left[0] - 10;
    t->top_right[0] = t->top_right[0] - 10;
    t->bot_right[0] = t->bot_right[0] - 10;
    t->bot_left[0] = t->bot_right[0] - 10;
    drawObject(t);
}

//Deprecated
void init_anim(object_reg *t){
    clearObject(&user_ship);
    //moveLeft(&user_ship);
    drawObject(&user_ship);
    //clearObject(&user_ship);
    //moveLeft(&user_ship);
    //drawObject(&user_ship);
    //clearObject(&user_ship);
    //moveRight(&user_ship);
    //drawObject(&user_ship);
}

//Used to initilize the game.

void setSpawn(object_array * t){
    for(int i = 0; i<NUM_ENEMIES; i++){
        int offset = i*40;
        t->objects[i].draw = 1;
        t->objects[i].top_left[0] =100+offset;
        t->objects[i].top_left[1] =71;
        t->objects[i].top_right[0] = 111+offset;
        t->objects[i].top_right[1] = 71;
        t->objects[i].bot_left[0] = 100+offset;
        t->objects[i].bot_left[1] = 60;
        t->objects[i].bot_right[0] = 111+offset;
        t->objects[i].bot_right[1] = 60;
    }
}
void setSpawnBig(object_array_b *t){
    for(int i = 0; i<NUM_ENEMIES; i++){
        int offset = i*40;
        t-> objects[i].draw = 1;
        t->objects[i].top_left[0] =100+offset;
        t->objects[i].top_left[1] =108;
        t->objects[i].top_right[0] = 111+offset;
        t->objects[i].top_right[1] = 108;
        t->objects[i].bot_left[0] = 100+offset;
        t->objects[i].bot_left[1] = 90;
        t->objects[i].bot_right[0] = 111+offset;
        t->objects[i].bot_right[1] = 90;
    }
}
//Sets the Spawn location of the user ship also erases Any Shot user bullets

void setSpawnUser(object_reg * t, object_array * bullets){
    clearObject(t);
    t->top_left[0] = 200;
    t->top_left[1] = 320;

    t->top_right[0] = 211;
    t->top_right[1] = 320;

    t->bot_left[0] = 200;
    t->bot_left[1] = 331;

    t->bot_right[0] = 211;
    t->bot_right[1] = 331;
    for(int i= 0; i<5; i++){
        bullets->objects[i].draw = 0;
        bullets->objects[i].shot = 0;
        clearObject(&(bullets->objects[i]));
    }
    drawObject(t);
}
//Defines movemenvet for both the big and small enemies.
void enemyMovement(object_array *t, int rightorLeft){
    for(int i = 0; i<NUM_ENEMIES; i++){
        clearObject(&(t->objects[i]));
    }
    for(int i = 0; i<NUM_ENEMIES; i++){
        if(rightorLeft){
            t->objects[i].top_left[0] += 1;
            t->objects[i].top_right[0] += 1; 
            t->objects[i].bot_left[0] += 1;
            t->objects[i].bot_right[0] += 1;
            move_step++;
        }
        else{
            t->objects[i].top_left[0] += -1;
            t->objects[i].top_right[0] += -1; 
            t->objects[i].bot_left[0] += -1;
            t->objects[i].bot_right[0] += -1;
            move_step--;
        }
    }
    for(int i = 0; i<NUM_ENEMIES; i++){
        drawObject(&(t->objects[i]));
    }  
}
void enemyMovementBig(object_array_b *t, int rightorLeft){
    for(int i = 0; i<NUM_ENEMIES; i++){
            clearObjectBig(&(t->objects[i]));
        }
        for(int i = 0; i<NUM_ENEMIES; i++){
            if(rightorLeft){
                t->objects[i].top_left[0] += 1;
                t->objects[i].top_right[0] += 1; 
                t->objects[i].bot_left[0] += 1;
                t->objects[i].bot_right[0] += 1;
                move_step++;
            }
            else{
                t->objects[i].top_left[0] += -1;
                t->objects[i].top_right[0] += -1; 
                t->objects[i].bot_left[0] += -1;
                t->objects[i].bot_right[0] += -1;
                move_step--;
            }
        }
        for(int i = 0; i<NUM_ENEMIES; i++){
            drawObjectBig(&(t->objects[i]));
        }  
}

// Shoots a bullet that hasn't been shot yet
void shoot(object_reg *t, object_array *y, int direction){
    for(int i = 0; i<NUM_ENEMIES;i++){
        if(direction == 1){
            if(y->objects[i].shot == 0 && y->objects[i].draw == 0 && t->draw == 1){
                y->objects[i].bot_left[0] = t->bot_left[0];
                y->objects[i].bot_left[1] = t->bot_left[1];
                y->objects[i].bot_right[0] = t->bot_right[0];
                y->objects[i].bot_right[1] = t->bot_right[1];
                y->objects[i].top_left[1] = t->top_left[1];
                y->objects[i].top_right[1] = t->top_right[1];
                y->objects[i].top_left[0] = t->top_left[0];
                y->objects[i].top_right[0] = t->top_right[0];
                y->objects[i].draw = 1;
                y->objects[i].shot = 1;
                drawObject(&(y->objects[i]));
                break;
            }
        }
        else{
            if(y->objects[i].shot == 0 && y->objects[i].draw == 0 && t->draw == 1){
                y->objects[i].bot_left[0] = t->bot_left[0];
                y->objects[i].bot_left[1] = t->bot_left[1];
                y->objects[i].bot_right[0] = t->bot_right[0];
                y->objects[i].bot_right[1] = t->bot_right[1];
                y->objects[i].top_left[1] = t->top_left[1];
                y->objects[i].top_right[1] = t->top_right[1];
                y->objects[i].top_left[0] = t->top_left[0];
                y->objects[i].top_right[0] = t->top_right[0];                
                y->objects[i].draw = 1;
                y->objects[i].shot = 1;
                shoot_bullet = 0;
                
                drawObject(&(y->objects[i]));
                break;
            }
        }
    }
}
void shootBig(object_big *t, object_array_b *y, int direction){
    for(int i = 0; i<5;i++){
        if(direction == 1){
            if(y->objects[i].shot == 0 && y->objects[i].draw == 0 && t->draw == 1){
                y->objects[i].bot_left[0] = t->bot_left[0];
                y->objects[i].bot_left[1] = t->bot_left[1];
                y->objects[i].bot_right[0] = t->bot_right[0];
                y->objects[i].bot_right[1] = t->bot_right[1];
                y->objects[i].top_left[1] = t->top_left[1];
                y->objects[i].top_right[1] = t->top_right[1];
                y->objects[i].top_left[0] = t->top_left[0];
                y->objects[i].top_right[0] = t->top_right[0];
                y->objects[i].draw = 1;
                y->objects[i].shot = 1;
                //printf("drawing\n");
                drawObjectBig(&(y->objects[i]));
                break;
            }
        }
    }
}

void bullet_move(object_array *y, int direction){
    for(int i = 0; i<NUM_ENEMIES; i++){
        if(direction == 1){
            if(y->objects[i].shot == 1 && y->objects[i].draw == 1){
                clearObject(&(y->objects[i]));
                y->objects[i].bot_left[1] += 3;
                y->objects[i].bot_right[1] +=3;
                y->objects[i].top_left[1] += 3;
                y->objects[i].top_right[1] += 3;
                drawObject(&(y->objects[i]));
            }
        }
        else{
            if(y->objects[i].shot == 1 && y->objects[i].draw == 1){
                clearObject(&(y->objects[i]));
                y->objects[i].bot_left[1] += -3;
                y->objects[i].bot_right[1] +=-3;
                y->objects[i].top_left[1] += -3;
                y->objects[i].top_right[1] += -3;
                drawObject(&(y->objects[i]));
            }
        }
    }
}
void bullet_move_big(object_array_b *y, int direction){
    for(int i = 0; i<NUM_ENEMIES; i++){
        if(direction == 1){
            if(y->objects[i].shot == 1 && y->objects[i].draw == 1){
                clearObjectBig(&(y->objects[i]));
                y->objects[i].bot_left[1] += 3;
                y->objects[i].bot_right[1] +=3;
                y->objects[i].top_left[1] += 3;
                y->objects[i].top_right[1] += 3;
                drawObjectBig(&(y->objects[i]));
            }
        }
    }
}

void hit(object_array * bullets, object_array * e_bullets, object_array * small_enemy, object_array_b * big_enemy, object_array_b * bigBullets, object_reg * user){
    
    for(int i = 0; i<NUM_ENEMIES; i++){
        if(i == 0){
            printf("\n%d", bullets->objects[i].bot_left[1]);
            printf("\n%d", bullets->objects[i].bot_right[1]);

            printf("\n%d", bullets->objects[i].top_left[1]);
            printf("\n%d", bullets->objects[i].top_right[1]);
            }
       
        // User Bullets verus enemies
        if(bullets->objects[i].shot == 1 && bullets->objects[i].draw == 1){
            for(int k = 0; k<NUM_ENEMIES; k++){
                
                if((bullets->objects[i].bot_left[1] <= small_enemy->objects[k].top_left[1] && bullets->objects[i].top_left[1] <= small_enemy->objects[k].bot_left[1])
                && ((bullets->objects[i].bot_left[0] >= small_enemy->objects[k].bot_left[0] && 
                    bullets->objects[i].bot_left[0] <= small_enemy->objects[k].bot_right[0]) || ( 
                    bullets->objects[i].bot_right[0] >= small_enemy->objects[k].bot_left[0] && 
                    bullets->objects[i].bot_right[0] <= small_enemy->objects[k].bot_right[0]))
                    && small_enemy->objects[k].draw == 1
                ){
                    bullets->objects[i].draw = 0; 
                    bullets->objects[i].shot = 0;
                    small_enemy->objects[k].draw = 0;
                    score += 100;
                    ammo--;
                    playHitSound = 1;
                    printf("\nHitsmall");
                    clearObject(&(small_enemy->objects[k]));
                    clearObject(&(bullets->objects[i]));
                    break;
                }

                if((bullets->objects[i].bot_left[1] <= big_enemy->objects[k].top_left[1] && bullets->objects[i].top_left[1] <= big_enemy->objects[k].bot_left[1])
                && ((bullets->objects[i].bot_left[0] >= big_enemy->objects[k].bot_left[0] && 
                    bullets->objects[i].bot_left[0] <= big_enemy->objects[k].bot_right[0]) || ( 
                    bullets->objects[i].bot_right[0] >= big_enemy->objects[k].bot_left[0] && 
                    bullets->objects[i].bot_right[0] <= big_enemy->objects[k].bot_right[0]))
                    && big_enemy->objects[k].draw == 1
                ){
                    bullets->objects[i].draw = 0; 
                    bullets->objects[i].shot = 0;
                    big_enemy->objects[k].draw = 0;
                    score += 400;
                    ammo--;
                    playHitSound = 1;
                    printf("\nHit");
                    clearObjectBig(&(big_enemy->objects[k]));
                    clearObject(&(bullets->objects[i]));
                    break;  
                }
            }
            if(bullets->objects[i].bot_left[1] <= 50){ //if the bullet went out of bounds clear it
                bullets->objects[i].draw = 0;
                bullets->objects[i].shot = 0;
                ammo--;
                clearObject(&(bullets->objects[i]));
            }
        }
        
        //Small Enemies bullets verus user
        if(e_bullets->objects[i].draw == 1 && e_bullets->objects[i].shot == 1){
            if((e_bullets->objects[i].top_left[1] >= user->bot_left[1] && e_bullets->objects[i].bot_left[1] <= user->bot_left[1])
                &&  ((e_bullets->objects[i].top_left[0] >= user->top_left[0] && e_bullets->objects[i].top_left[0] <= user->top_right[0]) 
                || (e_bullets->objects[i].top_right[0] >= user->top_left[0] && e_bullets->objects[i].top_right[0]<=user->top_right[0])))
            {
                lives--;
                pause = 1;
                died = 1;
                e_bullets->objects[i].draw = 0;
                e_bullets->objects[i].shot = 0;
                playHitSound = 1;
                clearObject(&(e_bullets->objects[i]));

            }
            //printf("\n%d", e_bullets->objects[i].bot_left[1]);
            if(e_bullets->objects[i].bot_left[1] >= 335){
                e_bullets->objects[i].draw = 0;
                e_bullets->objects[i].shot = 0;
                clearObject(&(e_bullets->objects[i]));
            }
        }
    
        ///Big bullets versus user
        if(bigBullets->objects[i].draw == 1 && bigBullets->objects[i].shot == 1){
            
            if(((bigBullets->objects[i].top_left[1] >= user->bot_left[1] && bigBullets->objects[i].bot_left[1] <= user->bot_left[1])
                &&  ((bigBullets->objects[i].top_left[0] >= user->top_left[0] && bigBullets->objects[i].top_left[0] <= user->top_right[0]) 
                || (bigBullets->objects[i].top_right[0] >= user->top_left[0] && bigBullets->objects[i].top_right[0]<=user->top_right[0]))))
            // (
                
            //     bigBullets->objects[i].top_left[1] >= user->bot_left[1]
            //     &&  (bigBullets->objects[i].top_left[0] >= user->top_left[0] && bigBullets->objects[i].top_left[0] <= user->top_right[0])
            //     )
            {
                lives--;
                pause = 1;
                died = 1;
                bigBullets->objects[i].draw = 0;
                bigBullets->objects[i].shot = 0;
                playHitSound = 1;
                clearObjectBig(&(bigBullets->objects[i]));
            }
            if(bigBullets->objects[i].bot_left[1] >= 335){
                bigBullets->objects[i].draw = 0;
                bigBullets->objects[i].shot = 0;
                clearObjectBig(&(bigBullets->objects[i]));
            }
        }
    }
}

void restart(){
    for(int i = 0; i<NUM_ENEMIES; i++){
        clearObject(&(enemy_objects.objects[i]));
        clearObjectBig(&(enemy_objects2.objects[i]));
        clearObject(&(bullets_objects.objects[i]));
        clearObject(&(enemy_bullets.objects[i]));
        clearObjectBig(&(big_enemy_bullets.objects[i]));
        big_enemy_bullets.objects[i].draw = 0;
        big_enemy_bullets.objects[i].shot = 0;

        enemy_bullets.objects[i].draw = 0;
        enemy_bullets.objects[i].shot = 0;

    }
    setSpawn(&enemy_objects);
    setSpawnBig(&enemy_objects2);
    setSpawnUser(&user_ship, &bullets_objects);
    died = 0;
    score = 0;
    lives = 3;
    ammo = 0;
    win = 0;
    move_step = 0;
    
}

void user_ship_move_to(object_reg *t, int x){
    int diff = abs(x - last_ship_pos);
    float position;


    if(diff > 40){
        position = x; 
    }

    position = (2.2*position);

    if(position < 55){
        position = 55;
    }else if(position > 575){
        position = 564;
    }
    clearObject(t);
    t->bot_left[0] = (int)(position);
    t->top_left[0] = (int)(position);
    t->bot_right[0]= (int)(position)+11;
    t->top_right[0]= (int)(position)+11;
   
    drawObject(t);
    last_ship_pos = (int)position;
}

void hasWon(object_array * smallE, object_array_b * bigE){
    for(int i = 0; i<NUM_ENEMIES; i++){
        if(smallE->objects[i].draw == 1 || bigE->objects[i].draw == 1){
            return;
        }
    }
    win = 1;
    pause = 1;
}


static PT_THREAD(protothread_animate(struct pt *pt)){
    PT_BEGIN(pt);  
    static int begin_time;
    static int spare_time;
    static int RrL;
    static int pleaseDraw = 0;
    static int pleaseDraw2=0;
    static int enemyshoot =0;
    static int lastScore =0;

    
    while(1){
        begin_time = time_us_32();
        if(pause==0){ //if the game is not paused run. if it is paused dont do anything.
            drawBoundary();
            if(move_step<0){
                RrL = 1; 
            }
            if(move_step>1000){
                RrL = 0;
            }
            if(ammo<5 && shoot_bullet == 1){ //To shoot, currently input 1 into the serial monitor
                                            // This code should only allow one bullet at a time for a max of 5 on screen.
                //printf("\nShot");
                shoot(&user_ship, &bullets_objects, 0);
                ammo++;
            }
            if(pleaseDraw2 == 20){
                for(int i = 0; i<NUM_ENEMIES; i++){
                    if(RrL){
                        enemyMovementBig(&enemy_objects2,RrL);
                    }
                    else{
                        enemyMovementBig(&enemy_objects2,RrL);
                    }
                }
                pleaseDraw2 = 0;
            }
            if(pleaseDraw == 10){
                for(int i = 0; i<NUM_ENEMIES; i++){
                    if(RrL){
                        enemyMovement(&enemy_objects,RrL);
                    }
                    else{
                        enemyMovement(&enemy_objects,RrL);
                    }
                }
                pleaseDraw = 0;
            }
            if(enemyshoot == 1){
                int ran = rand()%(10);
               // printf("\n%d", ran);
                int ran2 = rand()%(10);
                //printf("\n%d", ran2);
                if(ran > 1){ 
                    shoot(&(enemy_objects.objects[ran2]), &enemy_bullets, 1);
                }
                if(ran == 1){
                    shootBig(&(enemy_objects2.objects[ran2]), &big_enemy_bullets, 1);
                }
                enemyshoot=0;
            }
            enemyshoot++;
            pleaseDraw++;
            pleaseDraw2++;
            bullet_move(&bullets_objects, 0);
            bullet_move(&enemy_bullets, 1);
            bullet_move_big(&big_enemy_bullets,1);
            for(int k = 0; k<NUM_ENEMIES; k++){
                drawObject(&(enemy_objects.objects[k]));
                drawObjectBig(&(enemy_objects2.objects[k]));
            }
            drawObject(&user_ship);
            hit(&bullets_objects, &enemy_bullets, &enemy_objects, &enemy_objects2, &big_enemy_bullets, &user_ship); //Hit detection
            user_ship_move_to(&user_ship, pos[0]);
            hasWon(&enemy_objects, &enemy_objects2);
        }
        else{
            if(pause == 1 && died == 0 && win == 0){
                drawUnpause();
                drawPause();
            }
            else if(pause == 1 && died == 1 && win == 0){
                if(lives == 0){
                    drawLost();
                }
                else{
                    drawDied();
                }
            }
            else if(pause == 1 && died == 0 && win == 1){
                drawWin();
            }
        }
        if(score != lastScore){
            
        }
        lastScore = score;
        spare_time = FRAME_RATE - (time_us_32() - begin_time);
        drawInfo(score, lives, spare_time);
        PT_YIELD_usec(spare_time);
        
    }
    PT_END(pt);
}


static PT_THREAD (protothread_serial(struct pt *pt)){
    PT_BEGIN(pt);
    // stores user input
    static int user_input ;
    static float depth_input;
    // wait for 0.1 sec
    PT_YIELD_usec(1000000) ;
    // announce the threader version
    sprintf(pt_serial_out_buffer, "Protothreads RP2040 v1.0\n\r");
    // non-blocking write
    serial_write ;
      while(1) {
        // print prompt
        sprintf(pt_serial_out_buffer, "input a number in the range 1-2: ");
        // non-blocking write
        serial_write ;
        // spawn a thread to do the non-blocking serial read
        serial_read ;
        // convert input string to number
        sscanf(pt_serial_in_buffer,"%d", &user_input) ;
        if(!gpio_get(15)){
           // printf("shoot\n");
            play = 1;
            playShootSound = 1;
            shoot_bullet = 1;
        }
        if(user_input ==2){
            pause = 1;
        }
        if(user_input == 3){
            pause = 0;
            drawUnpause();
        }
        if(user_input == 4){
            moveRight(&user_ship);
        }
        if(user_input ==5){
            moveLeft(&user_ship);
        }
        if(user_input == 6){
            pause = 1;
            restart();
        }

      } // END WHILE(1)
  PT_END(pt);
}


static PT_THREAD (protothread_buttons(struct pt *pt)){
    PT_BEGIN(pt);
    // wait for 0.1 sec
    // announce the threader version
    while(1) {
        if(!gpio_get(15) && gpio_get(15) != lastb15){
            printf("shoot\n");
            playShootSound = 1;
            shoot_bullet = 1;
        }
        if(!gpio_get(14) && gpio_get(14) != lastb14){
            if(pause && lives >0){
                pause = 0;
                died = 0;
                drawUnpause();
            }else{
                pause =1;
            }
        }
        if(!gpio_get(13) && gpio_get(13) != lastb13){
            if(lives > 0){
                pause = 0;
                died = 0;
                drawUnpause();
            }
        }
        if(!gpio_get(12) && gpio_get(12) != lastb12){
            moveRight(&user_ship);
        }
        if(!gpio_get(11) && gpio_get(11) != lastb11){
            moveLeft(&user_ship);
        }
        if(!gpio_get(10) && gpio_get(10) != lastb10){
            pause = 1;
            restart();
        }

        lastb10 = gpio_get(10);
        lastb11 = gpio_get(11);
        lastb12 = gpio_get(12);
        lastb13 = gpio_get(13);
        lastb14 = gpio_get(14);
        lastb15 = gpio_get(15);


        PT_YIELD_usec(30000) ;
      } // END WHILE(1)
  PT_END(pt);
}

bool shootSound(){
    if (STATE_0 == 0) {
        // DDS phase and sine table lookup
        float foutc = 2*pow(10,-4)*pow(count_0,1)+2000;
        volatile unsigned int phase_incr_chirp = (foutc*107374.1824);;

        phase_accum_main_0 += phase_incr_chirp;
        DAC_output_0 = fix2int15(multfix15(current_amplitude_0,
            sin_table[phase_accum_main_0>>24])) + 2048 ;

        // Ramp up amplitude
        if (count_0 < ATTACK_TIME) {
            current_amplitude_0 = (current_amplitude_0 + attack_inc) ;
        }
        // Ramp down amplitude
        else if (count_0 > BEEP_DURATION - DECAY_TIME) {
            current_amplitude_0 = (current_amplitude_0 - decay_inc) ;
        }

        // Mask with DAC control bits
        DAC_data_0 = (DAC_config_chan_B | (DAC_output_0 & 0xffff))  ;

        // SPI write (no spinlock b/c of SPI buffer)
        spi_write16_blocking(SPI_PORT, &DAC_data_0, 1) ;

        // Increment the counter
        count_0 += 1 ;

        // State transition?
        if (count_0 == 2000) {
            STATE_0 = 1 ;
            count_0 = 0 ;
            play = 0;
        }
    }

    // State transition?
    else {
        count_0 += 1 ;
        if (count_0 == 10000) {
            current_amplitude_0 = 0 ;
            STATE_0 = 0 ;
            count_0 = 0 ;
            playShootSound = 0;
        }
    }

    // retrieve core number of execution
    corenum_1 = get_core_num() ;
    
    return true;
}

bool hitSound( ){
    if (STATE_0 == 0) {
        float foutc = 10*pow(count_0,1)+2000;
        volatile unsigned int phase_incr_chirp = (foutc*107374.1824);;

        phase_accum_main_0 += phase_incr_chirp;
        DAC_output_0 = fix2int15(multfix15(current_amplitude_0,
            sin_table[phase_accum_main_0>>24])) + 2048 ;

        // Ramp up amplitude
        if (count_0 < ATTACK_TIME) {
            current_amplitude_0 = (current_amplitude_0 + attack_inc) ;
        }
        // Ramp down amplitude
        else if (count_0 > BEEP_DURATION - DECAY_TIME) {
            current_amplitude_0 = (current_amplitude_0 - decay_inc) ;
        }

        // Mask with DAC control bits
        DAC_data_0 = (DAC_config_chan_B | (DAC_output_0 & 0xffff))  ;

        // SPI write (no spinlock b/c of SPI buffer)
        spi_write16_blocking(SPI_PORT, &DAC_data_0, 1) ;

        // Increment the counter
        count_0 += 1 ;

        // State transition?
        if (count_0 == 2000) {
            STATE_0 = 1 ;
            count_0 = 0 ;
            play = 0;
        }
    }

    // State transition?
    else {
        count_0 += 1 ;
        if (count_0 == 10000) {
            current_amplitude_0 = 0 ;
            STATE_0 = 0 ;
            count_0 = 0 ;
            playHitSound = 0;
        }
    }

    // retrieve core number of execution
    corenum_1 = get_core_num() ;
    
    return true;   
}

bool repeating_timer_callback_core_1(struct repeating_timer *t){
    if(playShootSound == 1){    
        shootSound();
    }
    if(playHitSound == 1){
        hitSound();
    }
    if(playDeathSound == 1){
        //deathSound();
    }
}

static PT_THREAD (protothread_core_1_blinky(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;
    while(1) {
        // Toggle on LED
        gpio_put(LED, !gpio_get(LED)) ;
        pos[0] = adaFruitRead_pos(pos);
        // Yield for 500 ms
        PT_YIELD_usec(10000) ;
    }
    // Indicate thread end
    PT_END(pt) ;
}

void initGPIOS(){
    gpio_init(10);
    gpio_pull_up(10);
    gpio_set_dir(10, GPIO_IN);

    gpio_init(11);
    gpio_pull_up(11);
    gpio_set_dir(11, GPIO_IN);

    gpio_init(12);
    gpio_pull_up(12);
    gpio_set_dir(12, GPIO_IN);

    gpio_init(13);
    gpio_pull_up(13);
    gpio_set_dir(13, GPIO_IN);

    gpio_init(14);
    gpio_pull_up(14);
    gpio_set_dir(14, GPIO_IN);

    gpio_init(15);
    gpio_pull_up(15);
    gpio_set_dir(15, GPIO_IN);
}

void initI2CSPI(){
    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000) ;
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;

    i2c_init(I2C_CHAN, I2C_BAUD_RATE) ;
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C) ;
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C) ;


    adaFruitReset();
    adaFruitRead_pos(pos);

    // Map LDAC pin to GPIO port, hold it low
    gpio_init(LDAC) ;
    gpio_set_dir(LDAC, GPIO_OUT) ;
    gpio_put(LDAC, 0) ;

    // Map LED to GPIO port, make it low
    gpio_init(LED) ;
    gpio_set_dir(LED, GPIO_OUT) ;
    gpio_put(LED, 0) ;
}

void core1_main(){
    alarm_pool_t *core1pool;
    core1pool = alarm_pool_create(2,16);

    struct repeating_timer timer_core_1;

    alarm_pool_add_repeating_timer_us(core1pool, -25,
    repeating_timer_callback_core_1, NULL, &timer_core_1);
    pt_add_thread(protothread_core_1_blinky);
    pt_schedule_start;
}

int main(){

    stdio_init_all();
    initVGA();

    initGPIOS();
    initI2CSPI();

    // set up increments for calculating bow envelope
    attack_inc = divfix(max_amplitude, int2fix15(ATTACK_TIME)) ;//max_amplitude/(float)ATTACK_TIME ;
    decay_inc =  divfix(max_amplitude, int2fix15(DECAY_TIME)) ;//max_amplitude/(float)DECAY_TIME ;

    

    // === build the sine lookup table =======
    // scaled to produce values between 0 and 4096
    int ii;
    for (ii = 0; ii < sine_table_size; ii++){
         sin_table[ii] = float2fix15(2047*sin((float)ii*6.283/(float)sine_table_size));
    }

    for(int ob = 0; ob<NUM_ENEMIES; ob++){
        enemy_objects.objects[ob] = enemy10;
        enemy_objects2.objects[ob] = bigEnemy;
        bullets_objects.objects[ob] = bullet;
        enemy_bullets.objects[ob] = enemy_bullet;
        big_enemy_bullets.objects[ob] = bigBullet;
    }
    for(int reeeeee = 0; reeeeee<NUM_ENEMIES; reeeeee++){
        bullets_objects.objects[reeeeee].draw = 0; 
        bullets_objects.objects[reeeeee].shot = 0;
        clearObject(&(bullets_objects.objects[reeeeee]));
    }
    setSpawn(&enemy_objects);
    setSpawnBig(&enemy_objects2);
    setSpawnUser(&user_ship, &bullets_objects);

    multicore_reset_core1();
    multicore_launch_core1(core1_main);

    pt_add_thread(protothread_serial);
    pt_add_thread(protothread_animate);
    pt_add_thread(protothread_buttons);
    pt_schedule_start ;


} 
        </pre>
    </body>
</html>