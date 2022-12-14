# Microcontrollers Final Project: 
# Dino Jump Video Game

## Introduction
I created a joystick-based video game using an IMU, microcontroller, and VGA monitor. Why? Because who doesn't like old-fashioned, arcade-style video games. The game has basic features of an arcade game: a cool welcome screen, pixelated graphics, a leaderboard that keeps track of high scores, pause/resume functionality, and a joystick to control the main character. The goal: get as high as possible and avoid getting hit by cannonballs. 

## High Level Design
Dino Jump is heavily inspired by the Doodle Jump app, except the graphics are more reminiscent of a retro arcade game, and the main character is a cool dinosaur named Ava. 

![IMG_7996](https://user-images.githubusercontent.com/71809396/207230842-d4444a76-e87b-4e85-be8b-4a7bdcb20986.png)

To play the game, you enter your name on the keyboard and press enter. This name is stored so that your score can be added to the leaderboard, assuming you score well enough. The "joystick" is an IMU mounted on a hinge that allows the joystick to move around one axis. You have no control over Ava's vertical motion: she jumps when she lands on a pad, and she falls according to the laws of gravity. You do have control over her horizontal velocity, through the IMU. Tilting left gives her a velocity pointing to the left, and tilting right gives her a velocity to the right. The game continues until either a cannonball is hit or Ava misses a landing pad and falls off screen. During game play, the game can be paused and resumed. Once the game is over (or before a game is begun), the user can enter 'l' to see a leaderboard containing the names and scores of the top five players. 

## Program/Hardware Design
### Hardware
The hardware setup for this project was simple. Everything connected to the RP2040 microcontroller via the Pico interface board. A picture is worth a thousand words, so here is how to setup the hardware.

<img width="571" alt="Screen Shot 2022-12-12 at 10 42 44 PM" src="https://user-images.githubusercontent.com/71809396/207221212-2aa24a23-33fa-4d6d-9554-5b4193673aac.png">

### Game Kinematics
Ava (the dinosaur) moves according to the laws of physics. Her position is updated according to the projectile motion equations you learned in high school. Of course, as in all good physical simulations, I ignored air resistance. Finding values for Ava's jump velocity and gravity that worked well with the game's frame rate of 30fps AND looked realistic was challenging. 
```
/* updates Ava's (x,y) and (vx, vy) according to projectile motion */
void dinoKinematics(){
  avavx = complementary_angle;
  if (avavx > int2fix(SPEED_LIMIT))
    avavx = int2fix(SPEED_LIMIT);
  else if (avavx < int2fix(SPEED_LIMIT))
    avavx = int2fix(-1 * SPEED_LIMIT);

  avavy += multfix(multfix(g, int2fix(time_kinematics)) + jump_y_vel;
  avax += multfix(int2fix(10), avavx);
  avay += multfix(half_g, int2fix(time_kinematics * time_kinematics)) + jump_y_vel;
}
```
In that block of code, you'll notice that I restrict the maximum speed Ava can move along the x-axis. ```complementary_angle``` is a global variable (gasp) that holds the most recent value reported by the IMU. You'll also notice that her y-velocity is updated according to projectile motion, where ```time_kinematics``` is a variable that increments every frame and resets every time Ava jumps. The last thing you should notice is that almost every kinematics computation uses fixed point operations on fixed point numbers. This is a huge savings in computation because fixed point operations are much faster than floating point. The update kinematics function for cannonballs is similar, so I won't include it here. In addition, a third function moves EVERYTHING in the environment down, to give the illusion that Ava is jumping to higher ground. 

### Game FSM
The game had these states: 
```
enum states{
  WELCOME,
  LEADERBOARD,
  PLAY,
  PAUSE,
  GAMEOVER
} state = WELCOME;
```
I trust that the reader can figure out what each of those states corresponds to. User input from the connected keyboard is relayed to the RP2040 and switches the state. The game automatically moves from state ```PLAY``` to ```GAMEOVER``` when the game is lost. 

<img width="587" alt="Screen Shot 2022-12-12 at 10 53 19 PM" src="https://user-images.githubusercontent.com/71809396/207222544-6c3713f4-c1d8-432f-bb5d-44524cc8fcff.png">

### Landing Pads
To keep track of landing pads, I used an nx2 array of fixed point numbers where n is the number of pads. To make the game playable forever, I used this code to continuously re-initialize the pad locations once they had disappeared from the bottom of the screen. 
```
/* Re-initializes pads somewhere above the dino (but not in the visible portion of the screen) if the pad has gone off the bottom of the screen */
void update_pads(){
  for (int i = 0; i < NUMPADS; i++){
    if (fix2int(pads[i][1]) > SCREEN_HEIGHT){
      pads[i][0] = int2fix(rand() % (SCREEN_WIDTH - PAD_WIDTH));
      // distance between pads increases as height grows.. game gets harder
      pads[i][1] = int2fix((rand() % (SCREEN_HEIGHT)) - SCREEN_HEIGHT - height / 100);
    }
  }
}
```
This means that I only needed n = 20 to give me plenty of landing pads, and I used very little memory on the RP2040. 

### IMU
To update the IMU, I used nearly the same code as lab 3; however, I no longer needed to trigger an interrupt that signaled an IMU read: I just read once per frame of the game (30 times per second). 
```
void updateIMU(){
  mpu6050_read_raw(acceleration, gyro);
  // Accelerometer angle (degrees - 15.16 fixed point)
  accel_angle = multfix15(divfix(acceleration[0], acceleration[1]), oneeightyoverpi);
  // Gyro angle delta (measurement times timestep) (15.16 fixed point)
  gyro_angle_delta = multfix15(gyro[2], zeropt001);
  complementary_angle = multfix15(complementary_angle - gyro_angle_delta, 35000) + multfix15(accel_angle, 200);
}
```
What are those weird values ```35000``` and ```200``` in the last line of my complementary filter? Those are the trial-and-error determined weighting values for the gyroscope and accelerometer. Those values gave me a response to change in the IMU that was strong but not too sensitive, and made the game easy to play. 

### Updating the Game
I won't include a full code section here, but I think it's worthwhile to mention what exactly occurs 30 times per second. Pseudocode for my ```updateFrame()``` is below:
```
updateFrame {
  read the IMU
  update the landing pads
  erase the dinosaur and cannonballs from the VGA
  update dinosaur and cannonball positions with kinematic eqns
  if the dino is off the screen or there's a cannonball collision:
    end the game
  if the dino is falling and lands on a pad:
    move everything down for a specified number of frames
  redraw the dinosaur and cannonballs
}
```
Obviously that's a simplification, but that is *generally* the flow of the update function. It gets called 30 times per second, which means that all those functions *must* be completed in less than 1/30th of a second. The frame rate is maintained by delaying the next call to ```updateFrame()``` by the amount of "spare time."

### Drawing
I used a ton of drawing functions to draw a left-facing Ava, right-facing Ava, landing pad, and cannonball. All of these relied on the ```drawRect()``` function included in the VGA graphics library from Hunter. Since draw rect is almost down to the level of individual pixels, it means that my ```drawDino()``` function was over 100 lines of code.  For text, I used the ```setCursor()```, ```setTextColor()```, ```setTextSize()```, and ```writeString()``` functions to write words to the monitor. 

### Threading & Concurrency
This program used two threads and two cores. In previous labs, the two cores had an approximately equal workload. For example, the boids simulation required that each core works on updating half of the boids flock, and one writes to the VGA while the other handles user input. This meant that I needed to be careful about sharing resources. For this assignment, I only needed two threads: one to run the game, and one to handle user input. 

<img width="588" alt="Screen Shot 2022-12-12 at 11 47 29 PM" src="https://user-images.githubusercontent.com/71809396/207229119-a48945e7-c1e1-45b0-bc78-b6db498be8b8.png">

This was a very easy division of labor because there were so few shared resources; however, it meant that core 1 was just sitting around idle for most of the time, polling for user input. If the game got complex enough that it required core 1 to do more work, I could replace the user input thread with a user input interrupt routine and use both cores to perform game updates, for example updating the dinosaur on one core and updating cannonballs on the other. 

## Results
Here's a video of the game in action.

<iframe width="560" height="315" src="https://www.youtube.com/embed/TZojMDk_-_0" frameborder="0" allow="autoplay; encrypted-media" allowfullscreen></iframe>

There are two main flaws with the current design: 
1. Flicker. I was unable to figure out what caused the dinosaur to occasionally flicker when it moves quickly. You can see this when the dinosaur jumps or falls quickly, and you can see it in the pads and dinosaur when the environment shifts down. 
2. Bounding box collision detection. Since I detect collisions with pads (and cannonballs) via a bounding box drawn around the dinosaur, you can have the dinosaur's feet be in free space but the dinosaur is still "on a pad" since a line drawn down from the dinosaur's snout *would be* on the pad. In addition, when the dinosaur moves very fast (like when in free fall) it could move so fast in one frame that it goes right through a pad. This rarely happens, but is possible. The fix would be to "add air resistance" so the dinosaur would have a terminal velocity slow enough to never travel the distance of a pad in one frame. 

## Conclusions
Overall this project was a success. The game is very playable, it looks good, and working on it made me happy. In addition, I had plenty of "spare time" to do additional computations while maintaining 30fps. This means that
1. my code is efficient (or, the RP2040 is a powerful microcontroller. Whichever.)
2. I have the ability to add in cool features and the frame rate won't be affected

One feature that I would have liked to add was sound effects. Most arcade games that I know of have funny sound effects, and it wouldn't have been too difficult to add a *boing* noise every time Ava jumped. Ultimately, like any software project, there is a tradeoff between deadlines and feature development. Unfortunately, the sound effect feature was a casualty of this tradeoff. 

As far as acknowledgements, nearly all the code written was my own. I did re-use my own complementary filter code for the IMU from Lab 3, and I used Hunter's VGA graphics library and several Pico hardware libraries for i2c and pio. Thank you to Hunter and Bruce for providing debugging suggestions and for giving me the knowledge necessary to complete this assignment!

## Appendix
I approve this report for inclusion on the course website.
I approve the video for inclusion on the course youtube channel.
