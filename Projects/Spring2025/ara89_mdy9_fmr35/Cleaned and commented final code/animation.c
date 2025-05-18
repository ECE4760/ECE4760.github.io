/**
* Base Code (animation.c) Author: Hunter Adams (vha3@cornell.edu)
*
*
* HARDWARE CONNECTIONS
*  - GPIO 16 ---> VGA Hsync
*  - GPIO 17 ---> VGA Vsync
*  - GPIO 18 ---> 470 ohm resistor ---> VGA Green
*  - GPIO 19 ---> 330 ohm resistor ---> VGA Green
*  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
*  - GPIO 21 ---> 330 ohm resistor ---> VGA Red
*  - RP2040 GND ---> VGA GND
*
* RESOURCES USED
*  - PIO state machines 0, 1, and 2 on PIO instance 0
*  - DMA channels (2, by claim mechanism)
*  - 153.6 kBytes of RAM (for pixel color data)
*
* Authors: Asma Ansari (ara89), Fiona Rae (fmr35), Emmy Yancey (mdy9)
*
*/

// Include the VGA grahics library
#include "vga16_graphics.h"
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
// Include Pico libraries
#include "pico/stdlib.h"
#include "pico/divider.h"
#include "pico/multicore.h"
// Include hardware libraries
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
// Include protothreads
#include "pt_cornell_rp2040_v1_3.h"
#include <time.h>

// Include ADC library
#include "hardware/adc.h"

// === the fixed point macros ========================================
typedef signed int fix15 ;
#define multfix15(a,b) ((fix15)((((signed long long)(a))*((signed long long)(b)))>>15))
#define float2fix15(a) ((fix15)((a)*32768.0)) // 2^15
#define fix2float15(a) ((float)(a)/32768.0)
#define absfix15(a) abs(a)
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a,b) (fix15)(div_s64s64( (((signed long long)(a)) << 15), ((signed long long)(b))))

// uS per frame
#define FRAME_RATE 33000

// Pico LED
#define LED_PIN 25

// Water Variables
#define W_GPIO 13          // water signal gpio pin
static char STATE_W = 0;   // Toggles the water state
static char WATER = 0;     // Water State

// Fertilizer Variables
#define F_GPIO 11         //fert signal
static char STATE_F = 0; // Toggles the Fert state
static char FERT = 0;    // Fert state

// Sun Variables
#define S_GPIO 28       //Sun intensity GPIO pin
#define S_Dir_GPIO 26   //Sun direction GPIO pin

// Sun intensity
static int Intensity_SUN = 10; // ADC-read value of the sun's intensity
static fix15 sunPosWeight = 0; 

// raw ADC value read from the "sun position" potentiometer
static int Dir_SUN = 0;

//set initial VGA coordinates of the sun at the middle of the screen. The y value here is constant
static int sunX = 320;
static int sunY = 50;

// Plant Tilt Variables
#define P_GPIO 27
static int plant_angle = 90;        //plant angle adc value
int plant_angle_degrees = -90;      // plant angle in integer degrees

// Rain
#define ay 0.7 // acceleration rate of rain

int waterSize = 0;  // size of the water rain object array
int maxWater = 100; // maximum number of water rain objects
int maxY = 482;     // Lowest y position for rain objects


int sun_radius = 10;            //radius of the sun graphic in VGA coordinates
int previous_Intensity_SUN = 0; //value of Intensity_SUN on the last frame

//sun position parameters
int previous_Dir_SUN = 0 ; // previous adc value of the sun's position
int previous_sunX = 320;  //previous x location of the sun

//Histogram Parameters
static int maxSun = 100; // maximum value of currSun
static int currSun = 50;  //current sun level [0,99]
static int currWater = 50; //current water level [0,99]
static int currFert = 0; //current fertilizer level [0,99]
static int maxYHist = 400; //Adjust this
static int waterXHist = 555; //x position of the water histogram bar
static int sunXHist = 610; //x position of the sun histogram bar
static int fertXHist = 500; //x position of the fertilizer histogram bar
static int width = 20; //histogram bar width
static int count = 0; //modded value for frame calculations

// ========================================
// TREE-RELATED CODE
// ========================================

// Maximum number of children per node
#define MAX_CHILDREN 4

// Forward declaration
typedef struct TreeNode TreeNode;

// Tree node structure
struct TreeNode {
  fix15 length; //length of the current tree node
  fix15 angle; //radian angluar direction of the current tree node with respect to VGA coordinates
  fix15 start_x; //starting x position of the node
  fix15 start_y; //starting y position of the node
  int num_children; //number of child nodes the current node has
  char is_leaf;     //if 0, this node is a branch, if 1, this node is a leaf
  TreeNode* children[MAX_CHILDREN]; //the array of child nodes the current tree has
};

// ==========================================================
// AI - MEMORY POOL SETUP
// ==========================================================
// Define size of the tree node pool
#define MAX_TREE_NODES 1750  // Adjust based on your memory constraints and needs
// Global pool of tree nodes
static TreeNode tree_node_pool[MAX_TREE_NODES];
// Tracks whether each node in the pool is used (1) or free (0)
static char tree_node_used[MAX_TREE_NODES];
// Number of nodes currently allocated from the pool
static int tree_nodes_allocated = 0;
// ==========================================================
// AI - MEMORY POOL SETUP
// ==========================================================

// Global tree structs
TreeNode* currentTree;


// ==========================================================
// AI - MEMORY POOL HELPER FUNCTIONS
// ==========================================================


// Initialize the tree node pool
void init_tree_node_pool() {
  // Mark all nodes as free
  for (int i = 0; i < MAX_TREE_NODES; i++) {
    tree_node_used[i] = 0;
  }
  // initialize allocated nodes to zero
  tree_nodes_allocated = 0;
}

// Get a free node from the pool
TreeNode* get_free_node() {
  // Find the first free node
  for (int i = 0; i < MAX_TREE_NODES; i++) {

    // when the first unmarked node is found
    if (!tree_node_used[i]) {
      // Mark as used and initialize all of its children to NULL
      tree_node_used[i] = 1;
      for (int j = 0; j < MAX_CHILDREN; j++) {
        tree_node_pool[i].children[j] = NULL;
      }
      //initialize the node's children to zero
      tree_node_pool[i].num_children = 0;

      //increment the number of trees allocated
      tree_nodes_allocated++;

      // return the first free node
      return &tree_node_pool[i];
    }
  }
  // No free nodes available
  return NULL;
}

// Return a node to the pool
void release_node(TreeNode* node) {
  //safe return in case of null pointer
  if (node == NULL) return;

  // find index of the current node
  int index = node - tree_node_pool;
  // if the index is greater or equal to zero and is less than the maximum
  if (index >= 0 && index < MAX_TREE_NODES) {
    //mark tree node as unused
    tree_node_used[index] = 0;
    //decrement the number of allocated tree nodes
    tree_nodes_allocated--;
  }
}

// Modified create_node function that uses the pool
TreeNode* create_node(fix15 length, fix15 angle, fix15 start_x, fix15 start_y, char is_leaf) {
  TreeNode* node = get_free_node();
  if (node == NULL) {
    // Handle out of memory - could return a minimal node or NULL
    return NULL;
  }
  // initialize node parameters 
  node->length = length;      //length of the current tree node
  node->angle = angle;        //radian angluar direction of the current tree node with respect to VGA coordinates
  node->start_x = start_x;    //starting x position of the node
  node->start_y = start_y;    //starting y position of the node
  node->num_children = 0;     //number of child nodes the current node has
  node->is_leaf = is_leaf;    //whether the current node is a leaf or a branch
  
  return node;
}

// Modified free_tree function to release nodes back to the pool
void free_tree(TreeNode* node) {
  if (node == NULL) return;
  
  // Free all children first
  for (int i = 0; i < node->num_children; i++) {
    if (node->children[i] != NULL) {
      free_tree(node->children[i]);
    }
  }
  
  // Return this node to the pool
  release_node(node);
}

// Check how many nodes are currently allocated
int get_allocated_node_count() {
  return tree_nodes_allocated;
}
// ==========================================================
// AI - MEMORY POOL HELPER FUNCTIONS
// ==========================================================

// Function to add a child to a node
void add_child(TreeNode* parent, TreeNode* child) {
  if (parent->num_children < MAX_CHILDREN) {
    parent->children[parent->num_children] = child;
    parent->num_children++;
  }
}

// Generate a tree with random branching
TreeNode* generate_tree(int depth, fix15 branch_length, fix15 leaf_length,
               fix15 branch_scale, fix15 leaf_scale) {
  if (depth == 0) {
    return create_node(leaf_length, int2fix15(0), int2fix15(0), int2fix15(0), 1);
  }
  // Create root branch
  TreeNode* root = create_node(branch_length, int2fix15(0), int2fix15(0), int2fix15(0), 0);
  // Randomly determine number of branches (2-4)
  int num_branches = (rand() % 3) + 2;
  // Generate child branches with random angles
  for (int i = 0; i < num_branches; i++) {
    // Random angle between -60 and 60 degrees
    fix15 branch_angle = int2fix15((rand() % 121) - 60);
    // Random scaling factor between 0.85 and 0.95
    fix15 random_scale = float2fix15(0.85 + ((float)(rand() % 10) / 100.0));
    // Some branches might be shorter
    fix15 length_modifier = float2fix15(0.8 + ((float)(rand() % 20) / 100.0));
    TreeNode* child = generate_tree(
        depth - 1,
        multfix15(multfix15(branch_length, branch_scale), length_modifier),
        multfix15(multfix15(leaf_length, leaf_scale), length_modifier),
        multfix15(branch_scale, random_scale),
        multfix15(leaf_scale, random_scale));
    child->angle = branch_angle;
    add_child(root, child);
  }
  return root;
}

// Function to draw a tree using VGA library
void draw_tree(TreeNode* node) {
  if (node == NULL) return;

  float angle_rad = fix2float15(node->angle);
  fix15 end_x = node->start_x + multfix15(node->length, float2fix15(cos(angle_rad)));
  fix15 end_y = node->start_y + multfix15(node->length, float2fix15(sin(angle_rad)));

  // Set color based on node type
  char color = node->is_leaf ? PINK : DARK_ORANGE;

  // Draw the current segment
  drawLine(fix2int15(node->start_x), fix2int15(node->start_y),
           fix2int15(end_x), fix2int15(end_y), color);

  // Update the children’s start positions before recursion
  for (int i = 0; i < node->num_children; i++) {
    if (node->children[i] != NULL) {
      node->children[i]->start_x = end_x;
      node->children[i]->start_y = end_y;
      draw_tree(node->children[i]);
    }
  }
}

//pi in fix
#define PI float2fix15(3.14159265359)

//conversion factor from fix degrees to fix radians
#define degree2radian multfix15(divfix(int2fix15(1), int2fix15(180)), PI)

//fix macros for calculation
#define one int2fix15(1)
#define one_percent divfix(int2fix15(1), int2fix15(100))

//term weights: they sum to one
#define balanced_weight divfix(int2fix15(2), int2fix15(10)) 
#define random_weight divfix(int2fix15(8), int2fix15(10))

// the angle given from the tilt converted to fix radians 
fix15 plant_angle_fix_rad;

//updates the given tree node
void update_tree(TreeNode* node, int n) {
  
  //safety return given a null pointer
  if (node == NULL) return;

  //generate an angular change between +30 degrees and -30 degrees. then convert to radians
  fix15 random_component = multfix15(int2fix15((rand() %121) - 60), degree2radian);

  //add this component to the angle of the previous branch for the random angle
  fix15 r = node->angle + random_component;

  // balancing term between sunlight and tilt
  fix15 p = multfix15(int2fix15(currSun), one_percent); 

  // calculate the angle given from the tilt converted to fix radians 
  plant_angle_fix_rad = multfix15(int2fix15(plant_angle_degrees), degree2radian);

  
  // weighted terms for te net growth direction
  fix15 sunAngle_term = multfix15(balanced_weight, multfix15(p, sunPosWeight)); // coupled term for the sun componenet
  fix15 random_term = multfix15(r, random_weight); //rantomized vector term
  fix15 balanced_tilt = multfix15(multfix15(balanced_weight, (one-p)), plant_angle_fix_rad); // coupled term for tilt
  
  // sum to give the net growth direction for the next node
  fix15 angle = sunAngle_term + random_term + balanced_tilt;


  //when max recursion depth is reached, find the endpoints of the current branch 
  // and create a leaf starting from that poistion
  if (n == 8){
    fix15 end_x = node->start_x + multfix15(node->length, float2fix15(cos(fix2float15(node->angle))));
    fix15 end_y = node->start_y + multfix15(node->length, float2fix15(sin(fix2float15(node->angle))));
    add_child(node, create_node(int2fix15(5), angle, end_x, end_y, 1));
    return;
  }

  //When the current node is a branch that has not reached its maximum length and has no childred, increase its length by 5 pixels
  if((fix2int15(node->length) < (140.0/n*0.01*(float)currWater)) && (node->num_children == 0) && (node->is_leaf == 0)){
    node->length += int2fix15(5);
  }

  //when the current node is a branch and has a number on children less than the maximum
  else if(node->num_children <= MAX_CHILDREN && (node->is_leaf == 0) ){

    //calculate the sun balancing term
    fix15 p = multfix15(int2fix15(currSun), one_percent);

    //calculate the random angle component
    fix15 r = float2fix15(((rand() % 121) - 60)*3.1415/180.0);

    //char determining whether the next node is a leaf
    char leaf; 

    // if recursion depth is greater than 2, flip a coin to determine whether it's a leaf
    if (n > 2) leaf = (rand() % 2); 
    // below recursion depth 2, the node is always a branch
    else leaf = 0;

    // calculate the end position of the current node based on the angle parameter
    fix15 end_x = node->start_x + multfix15(node->length, float2fix15(cos(fix2float15(node->angle))));
    fix15 end_y = node->start_y + multfix15(node->length, float2fix15(sin(fix2float15(node->angle))));
    
    // add the child node based on the previously calculated angle, 
    // the end coordinates of the previous node, and the leaf char
    add_child(node, create_node(int2fix15(5), angle, end_x, end_y, leaf));
  }

  // Recursively update all children
  for (int i = 0; i < node->num_children; i++) {
      if (node->children[i] != NULL) {        
          update_tree(node->children[i], n+1);
      }
  }
}



// Initialize all buttons (Water = GPIO 22, Fertilizer = GPIO 23)
// Call in main()
void button_setup(){
  
  //fert button
  gpio_init(F_GPIO);
  gpio_set_dir(F_GPIO, GPIO_IN) ;
  gpio_pull_down(F_GPIO);

  //water button
  gpio_init(W_GPIO);
  gpio_set_dir(W_GPIO, GPIO_IN) ;
  gpio_pull_down(W_GPIO);
}




// ==================================================
// === Button State Helper Functions========================
// ==================================================

// Scan the Water Button and Set Global Water Flag to 1 if pressed or 0 if unpressed
void button_scan_water(){
  int i = gpio_get(W_GPIO);
  if (STATE_W == 0 && i == 1){
    STATE_W = 1;
    WATER = 1;
    gpio_put(LED_PIN, 1);
  }
  else if (STATE_W == 1 && i == 0){
    STATE_W = 0;
    WATER = 0;
    gpio_put(LED_PIN, 0);
  }
}

// Scan the Fertilizer Button and Set Global Fertilizer Flag to 1 if pressed or 0 if unpressed
void button_scan_fert(){
  char i = gpio_get(F_GPIO);
  if (STATE_F == 0 && i == 1){
    STATE_F = 1;
    FERT = 1;
    gpio_put(LED_PIN, 1);
  }
  else if (STATE_F == 1 && i == 0){
    STATE_F = 0;
    FERT = 0;
    gpio_put(LED_PIN, 0);
  }
}

// ==================================================
// === Digital Button reader thread
// ==================================================

static PT_THREAD (button_scan(struct pt *pt))
{
  PT_BEGIN(pt);
  // read the values of water and fert every frame
  while (1){
    button_scan_water();
    button_scan_fert();
    PT_YIELD_usec(33000);
  }
  PT_END(pt);
}

// ==================================================
// === ADC initialization============================
// ==================================================
void ADC_setup(){
  adc_init();
  // Make sure GPIO is high-impedance, no pullups etc
  adc_gpio_init(S_GPIO);
  adc_gpio_init(S_Dir_GPIO);
  adc_gpio_init(P_GPIO);
}

// ==================================================
// === Value reading helpers ========================
// ==================================================

//reads the raw values of the ADC channels into global space
void read_raw(){
  //rea sun direction
  adc_select_input(0);
  Dir_SUN = 4095 - adc_read();

  // read plant angle
  adc_select_input(1);
  plant_angle = 4095 - adc_read();

  // read sun intensity
  adc_select_input(2);
  Intensity_SUN = 4095 - adc_read();
}

// ==================================================
// === ADC reader thread
// ==================================================
static PT_THREAD (adcReader(struct pt *pt)){
  PT_BEGIN(pt);
  while(1){
    //read all raw ADC values to global space 
    read_raw();
    PT_YIELD_usec(33333); // wait for a single frame
  }
  PT_END(pt);
}

// ==================================================
// === RAIN STRUCTURE DECLARATIONS ==================
// ==================================================

// Animated struct for the Fertilizer and rain animations
// NOTE: BOTH VISUAL RAIN AND FERTILIZER ARE 
struct Rain{
float x, y, vy; // x-position, y-position, y-velocity
};

//Statically declared array of Rain objects representing water droplets
struct Rain water[100];

//Statically declared array of Rain objects representing water droplets
#define fert_max 50 //maximum amount of fertilizer objects
struct Rain fert[fert_max];

// ==================================================
// === RAIN-BASED HELPERS============================
// ==================================================

//take pointers to the attributes of a Rain struct and send it to the top of the screen 
//with a randomized x position and a constant y velocity downward
void spawn_rain(float* x, float* y, float* vy){
  *x = (rand() % 640);
  *y = 0;
  *vy = 5;
}


//take pointers to the attributes of a Rain struct and send it to the top of the screen 
//with a randomized x position in [200,440] and a 
//randomized y velocity downward in (float) [0,1]

void spawn_fert(float* x, float* y, float* vy){
  *x = 200 + (rand() % 240);
  *y = 0;
  *vy = (float)rand() / (float)RAND_MAX;
}

//Draws a black rectangle starting from VGA coordinate (x,y). Meant to cover Rain
//objects between frames
void erase_rain(float x, float y){
  drawRect((int)x, (int)y, 2, 2, BLACK);
}

//Draws a black rectangle starting from VGA coordinate (x,y). Meant to cover Rain
//objects between frames
void erase_fert(float x, float y){
  drawRect((int)x, (int)y, 2, 2, BLACK);
}

// ==================================================
// === RAIN WATER ANIMATION THREAD ==================
// ==================================================
static PT_THREAD (update_rain(struct pt *pt)){
  PT_BEGIN(pt);

  //recursion depth
  int n = 0;

  while(1){

    //When the length of the water array is less than the maximum and the Water state is on
    //erase the rain's previous position, and spawn it at the top
    //increment the maximum size of the water array
    if(waterSize < maxWater && WATER == 1){
      erase_rain(water[waterSize].x,  water[waterSize].y);
      spawn_rain(&water[waterSize].x, &water[waterSize].y, &water[waterSize].vy);
      waterSize = waterSize + 1;
    }

    //iterate over the water array
    for(int i = 0; i < maxWater; i++){

      //if the current rain object is below the lowest y position
      if(water[i].y >= maxY){

        //when the water state is on, respawn the current Rain object at the top of the sceen
        if(WATER == 1){
          erase_rain(water[i].x,  water[i].y);
          spawn_rain(&water[i].x, &water[i].y, &water[i].vy);
        }
        
        //otherwise erase the current rain object and set the rain array size to zero  
        else{
          erase_rain(water[i].x,  water[i].y);
          waterSize = 0;
        }
      }

      //when the current rain object's y position is less than or equal to maximum,
      //erase the previous position, set the new y position based on the y velocity, 
      //and draw the rain at it's new position
      if (water[i].y <= maxY){
        erase_rain(water[i].x,  water[i].y);
        water[i].y = water[i].y + water[i].vy;
        drawRect(water[i].x,  water[i].y, 2, 2, BLUE);
      }
    }
    PT_YIELD_usec(33333) // wait for a single frame
  }
  PT_END(pt);
}

// ==================================================
// === RAIN FERTILIZER ANIMATION THREAD =============
// ==================================================

static PT_THREAD (update_fert(struct pt *pt)){
  PT_BEGIN(pt);

  while(1){

    //iterate over the fertilizer rain object array
    for(int i = 0; i < fert_max; i++){

      //if the current Rain object is below the lowest y position
      if(fert[i].y >= maxY){

        //when the fert state is on, respawn the current Rain object at the top of the sceen
        if(FERT == 1){
          erase_fert(fert[i].x,  fert[i].y);
          spawn_fert(&fert[i].x, &fert[i].y, &fert[i].vy);
        }

        //otherwise erase the current rain object and set the rain array size to zero  
        else{
          erase_fert(fert[i].x,  fert[i].y);
        }
      }

      //when the current Rain object's y position is less than or equal to maximum,
      //erase the previous position, set the new y velocity based on the constant acceleration, ay, 
      //set the new y position based on the y velocity, and draw the rain at it's new position
      if (fert[i].y <= maxY){
        erase_fert(fert[i].x,  fert[i].y);
        fert[i].vy = fert[i].vy + ay;
        fert[i].y = fert[i].y + fert[i].vy;
        drawRect(fert[i].x,  fert[i].y, 2, 2, WHITE);
      }
    }
    PT_YIELD_usec(33333) // wait for a single frame
  }
  PT_END(pt);
}

// ==================================================
// === ANIMATION HELPERS ============================
// ==================================================


//plant angle parameters 
fix15 plant_angle_fix;

int previous_plant_angle_degrees = -90;  // previous angle in degrees

int previous_plant_angle = 0; // raw adc value of plant angle last frame

//Constants for value mapping
#define angle_scaling divfix(int2fix15(90), int2fix15(4095))
#define angle_offset -135

//Thresholds to passively filter the analog signals
#define DIR_SUN_THRESHOLD 100
#define INTENSITY_SUN_THRESHOLD 70
#define INTENSITY_TILT_THRESHOLD 30 



/*
INPUT PARAMETERS:
Dir_SUN: ADC read value of the sun's x-position
Intensity_SUN: ADC read value of the instensity value
plant_angle: ADC read value of the tilt parameter
OUTPUT MODIFICATIONS:
sun_radius: current value of the sun's intensity
sun_x: current value of the sun's x-position

SUN POSITION:
Take the ADC value of the sun position and map it to the proper locations on the VGA screen
This specifically modifies the center of the sun. The center should be able to be placed on all x positions of the
ACD values are in [0,4095] and they must be mapped to [0,639]: conversion rate of 5/32

SUN INTENSITY:
Take the ADC value of the sun intensity and map it to the the sun's minimum and maximum diameters.
This  modifies the size of the sun using the fill_circle() method in the VGA driver.
ACD values are in [0,4095] and they must be mapped to [0,47]: conversion rate of 3/256
This also updates the animation of the

PLANT ANGLE:
*/

void update_ADC_parameters(){

  
  //when a change in sun position exceeds the threshold
  if (abs(Dir_SUN - previous_Dir_SUN) > DIR_SUN_THRESHOLD){

    //push old values
    previous_Dir_SUN = Dir_SUN;
    previous_sunX = sunX;
    
    //multiply Dir_SUN right by 5 then BSR by 5
    sunX = (5 * Dir_SUN) >> 5;

    //calculate an Approximate angle pointing toward the sun
    //baased on the sun's current x poistion
    sunPosWeight = float2fix15((float)(sunX - 320)*0.00245) - float2fix15(1.5708);
  }

  //when a change in sun intensity exceeds the threshold
  if (abs(Intensity_SUN - previous_Intensity_SUN) > INTENSITY_SUN_THRESHOLD){

    //push old values
    previous_Intensity_SUN = Intensity_SUN;

    //multiply Intensity_SUN by 3 then BSR by 8 (gives sun radius)
    sun_radius = (3 * Intensity_SUN) >> 8;
  }

  // When a change in plant angle exceeds the threshold
  if (abs(plant_angle - previous_plant_angle) > INTENSITY_TILT_THRESHOLD){

    //push raw values
    previous_plant_angle = plant_angle;
     //push degree values
    previous_plant_angle_degrees = plant_angle_degrees;
   
    //save the fix of the current adc-read value
    plant_angle_fix = int2fix15(plant_angle);
   
    //convert the angle to degrees
    plant_angle_degrees = fix2int15(multfix15(angle_scaling, plant_angle_fix)) + angle_offset;
  
   }
   
}

//update histogram parameters
void updateHistogram(){
  // when the sun is under the lower static threshold and it's every 9th frame, decrement currSun by 1 until zero
  if(Intensity_SUN < 1750 && count%9 == 0){
    if(currSun > 0){
      currSun = currSun - 1;
    }
  }

  // when the sun is over the upper static threshold and it's every 7th frame, increment currSun until 99
  else if(Intensity_SUN > 2200 && count%7 == 0){
    if(currSun < 100){
      currSun = currSun + 1;
    }
  }

  //NOTE: between the static thresholds, currSun does not change

  // Interpreting water values

  //When the water button is pressed, below 100, and its every other frame, increment water
  if(WATER == 1 && currWater < 100 && count%2 == 0){
    currWater = currWater + 1;
  }
  //When the water button is unpressed, above 0, and its every 9th frame, decrement water
  else if (WATER == 0 && currWater > 0 && count%9 == 0){
    currWater = currWater - 1;
  }

  // When the FERT button is pressed and below 100, increment currFert every frame
  if(FERT == 1 && currFert < 100){
    currFert = currFert + 1;
  }
  //When the FERT button is unpressed, above 0, and its every 9th frame, decrement currFert
  else if (FERT == 0 && currFert > 0 && count%9 == 0){
    currFert = currFert - 1;
  }
  //increment count
  count++;
}

// ==================================================
// === HISTOGRAM ANIMATION THREAD ===================
// ==================================================

static PT_THREAD (protothread_hist(struct pt *pt))
{
  // Mark beginning of thread
  PT_BEGIN(pt);
  while(1){
    //update histogram parameters
    updateHistogram();
    
    // set the text size and color
    setTextSize(1);
    setTextColor(WHITE);

    //write the histogram parameter names
    setCursor(waterXHist, maxYHist - 20);
    writeString("Water");
    setCursor(sunXHist, maxYHist - 20);
    writeString("Sun");
    setCursor(fertXHist-25, maxYHist - 20);
    writeString("Fertilizer");

    // erase the histogram area
    fillRect(waterXHist, maxYHist, width, 100, BLACK);
    fillRect(sunXHist, maxYHist, width, 100, BLACK);
    fillRect(fertXHist, maxYHist, width, 100, BLACK);

    //draw new halues of the histogram
    drawRect(waterXHist, maxYHist+(100-currWater), width, 100, GREEN);
    drawRect(sunXHist, maxYHist+(100-currSun), width, 100, GREEN);
    drawRect(fertXHist, maxYHist+(100-currFert), width, 100, GREEN);

    //wait a frame
    PT_YIELD_usec(FRAME_RATE) ;
  }
  PT_END(pt);
}
// ==================================================
// === GENERAL ANIMATION THREAD ==================
// ==================================================

// Animation on core 0
static PT_THREAD (protothread_anim(struct pt *pt))
{
  // Mark beginning of thread
  PT_BEGIN(pt);

  // Variables for maintaining frame rate
  static int begin_time ;
  static int spare_time ;
  static int n = 0;

  while(1) {
  // Measure time at start of thread
  begin_time = time_us_32() ;

  // Update animation parameters
  update_ADC_parameters();

  //updating sun every 15 frames 
  int sunBuffer = 0;

  //when it's time to animate
  if (!(sunBuffer%15)){
    //erase previous sun position
    fillRect(previous_sunX-47, sunY-47, 192, 100, BLACK);
    //draw new sun
    drawCircle(sunX, sunY, sun_radius, ORANGE);
  }
  //increment the animation modded value
  sunBuffer++;

  // delay in accordance with frame rate
  spare_time = FRAME_RATE - (time_us_32() - begin_time);

  // yield for necessary amount of time
  PT_YIELD_usec(spare_time);

  }
  PT_END(pt);
} // animation thread

//amination rate at zero fertilizer
#define ANIMATION_RATE 150

// CORE 1 - TREE ANIMATION
static PT_THREAD (protothread_anim1(struct pt *pt))
{
  // Mark beginning of thread
  PT_BEGIN(pt);

  //value modded by animation rate - currfert to determine speed of animation
  static int i = 0; 

  //initialize the memory for tree storing
  init_tree_node_pool();
  
  //set the first tree node
  currentTree = create_node(int2fix15(5), float2fix15(-3.14159/2), int2fix15(320), int2fix15(480), 0);

  while (1) {
    //when it's tim to animate (frequency proportional to currfert)
    if(i%(ANIMATION_RATE-currFert)==0){
      drawRect(180, 240, 300, 240, BLACK);

      // Draw the tree (start from middle bottom of the screen, pointing up)
      draw_tree(currentTree);

      //Decide how the tree will grow 
      update_tree(currentTree, 1);

    }
    //increment modded value
    i++;

    //yield for a frame
    PT_YIELD_usec(33333);
  }

  PT_END(pt);
} // animation thread

// ========================================
// === core 1 main -- started in main below
// ========================================
void core1_main(){
  // Add animation thread
  pt_add_thread(protothread_anim1);
  // Start the scheduler
  pt_schedule_start ;
}


// ========================================
// === MAIN ===============================
// ========================================
int main(){
  // initialize stio
  stdio_init_all() ;

  // initialize VGA
  initVGA() ;

  // Initialize the LED pin
  gpio_init(LED_PIN);
  // Configure the LED pin as an output
  gpio_set_dir(LED_PIN, GPIO_OUT);

  // Initialize ADC
  ADC_setup();
  button_setup();

  // start core 1
  multicore_reset_core1();
  multicore_launch_core1(&core1_main);

  // add threads
  pt_add_thread(protothread_anim);
  pt_add_thread(button_scan);
  pt_add_thread(update_rain);
  pt_add_thread(update_fert);
  pt_add_thread(adcReader);
  pt_add_thread(protothread_hist);

  // start scheduler
  pt_schedule_start ;
}