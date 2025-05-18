## Final Project Report

<p align="center">
    Final Project Website for ECE 5730<br>
    Bryan Peters (bmp85), Martin Kostal (mk2378), Nikolai Nekrutenko (nan34)
</p>

<p align="center">
    <img width="600" alt="Drone" src="figs/drone.jpeg">
</p>

### Project Introduction

#### One sentence "sound bite" that describes your project
    
We built and implemented PID control on a Pico-powered quadcopter to hover on a gimbal stand with throttle input from a controller. 

#### A summary of what you did and why

We built a functional quadcopter system powered by a Raspberry Pi Pico, implementing a full PID-based flight controller from scratch to enable stable hover on a custom gimbal stand. Our goal was to design a hands-on, educational drone platform that could be integrated into ECE and MAE course curricula, allowing students to work directly with embedded systems, control theory, and dynamics. Starting from a partially developed hardware prototype and minimal software, we developed our own PID control logic, tuned it through extensive testing, and integrated radio-based throttle control. This project not only demonstrated our understanding of embedded flight control, but also laid the groundwork for a low-cost, reproducible drone platform for future student use.

### High level design

#### Rationale and sources of your project idea

Bryan, Martin and I enjoy drones and quadcopters, we thought it would be a neat idea to develop a somewhat functioning flight controller to control the drone with an onboard IMU for orientation data, an external remote to set the throttle position, and an electronic speed controller (ESC). 
    
Nikolai is a part of the Motion Studio "Drone Squad" advised by Dr. Beatriz Asfora which was involved in the creating of this final project idea. Out of the Drone Squad there was already a prototype developed by Deemo Chen with a PCB integrating an IMU and Pi Pico along with a skeleton of code on how to control each PWM ESC channel.

The long-term objective of this project is to support the integration of these Pico-based drones made from 3D printed components into ECE and MAE courses. For instance, these drones could be integrated in the ECE microcontrollers curriculum and the students could work on code developing/tuning a PID controller. Or, for a MAE dynamics course, the students could use these drones for dynamics modelling.

#### Background math

The effects of a $P$, $I$ and $D$ terms are well know. If we think of controlling the mass in a spring-mass-dashpot system, the $P$ term acts like increasing the stiffness of the spring, whereas the $D$ term effectively adds damping to the system. An $I$ term in general achieves the desirable property of having zero steady state. 

Even though our system is not a spring-mass-dashpot system, we can still keep in mind the above intuitive properties of each of the three terms of a PID controller. 
    
We ended up using the same set of three $P$, $I$, $D$ coefficients for controlling the pitch and the roll angle, and a different set of three $P$, $I$, $D$ coefficients for the yaw angle. Therefore, $6$ in total.

We did not do any modelling of the drone ("plant") dynamics, and did the tuning by trial and error. With more time, we would obtain the equations of motion and linearize about the desired hover position. We would get a linear set of equations, keeping in mind that we should include a model of noise in the measurements obtained from the IMU. There would be two standard approaches we could have taken: either design the controller using the state-space approach, or frequency design. Preceeding each of these two approaches, we would first have to obtain the dynamical equations of motion from Newton's second law and then linearize them about the desired hover position. 

Getting the dynamical equations of motion would have been non-trivial for several reasons, and was not achievable in the allocated time for this project. First of all, we would have to obtain the center of mass and tensor of inertia with respect to the gimbal, treating the drone as a rigid body. This could have been obtained from a CAD assembly, but only approximately - due to the geometry of $3D$ printed layers, see linked files for the individual parts. (Furthermore, the chip itself, the IMU, the wiring, the battery would all change these quantities, and indicate why it would have been really hard to obtain at least a reasonable model of the dynamics.) Second of all, the friction at the gimbal on the frame of the drone and the pull of the cables would have to be modelled. The thrust of the propellers in terms of the duty cycle of our PWM control signal supplied to the motors would be obtained from the documentation of the propeller's manufacturer. We should keep in mind that the gimbal support was not located under the center of mass. 

       
#### Logical structure
   
1. Initialize radio and sensors
2. Calibrate ESCs and Gyro
3. Initialize serial PID tuning interface
4. Wait for arm signal from controller
    1. Erase all store PID values -- effectively reset controls
5. Spin up motors to minimum throttle and begin executing PID loop
6. Continue executing PID loop until disarm

<p align="center">
    <img width="600" alt="DroneChart" src="figs/DroneFlowChart.png">
</p>

#### Hardware/software tradeoffs
    
- **PID loop speed:** Since the drone is a complicated, unstable system with 4 motors and 6 degrees of freedom, it is vital to have a fast control loop. From our testing and with lab 3, we found a value of 1kHz to work well. Compared to lab 3, we need to be computing the PID terms for 3 axes as opposed to a single PID axis. Those three axes are pitch, roll, and yaw. Pitch and roll have the same PID parameters and yaw has different PID parameters that will be discussed later in this section. 

- **Pico W:** The Pico W was used to allow future expansion of wireless capabilities. Future work could involve developing a web server that hosts an interactive PID tuning interface where students in courses could drag sliders and visualize the gyro output and controller output to see the performance of the PID developed or the drone modelled.

- **Controlling 4 Motors:** The PWM needs to be computed for each of the 4 motors, as opposed to a single axis like in lab 3, which takes more time to compute. This point is combined with the previous point made above, leading to significantly more computation involved. Essentially, 6 (PID terms) x 3 (axes) x 4 (motors) = 64 times the amount of computation relative to lab 3 since that lab had 1 axis and 1 motor. Additionally, changing the motor output has effects on the physical system, where changing the motor speed induces a torque on the drone, affecting the yaw axis, which will be discussed in the next section.

- **Yaw Compensation:** Since the drone is a physical system, we had to take into account physics for our software development and development. The quick and dirty solution that we came up with was having different PID weights for the yaw axis as opposed to the pitch and roll axes. For instance, the D term had to be greatly reduced as so the yaw compensation would not fight the other axes and enter an unstable jittery mess.

- **Long-Term Gyro Drift:** Gyro drift could have an effect on the longer-term characteristics in the minute to hour timescales. This is not much of a concern, however, as the drone only has battery life for 5 minutes at most due to the extreme power draw of flight. We implemented the gyro to calibrate everytime a battery is plugged in, so the drone must be set level.

#### Discuss existing patents, copyrights, and trademarks which are relevant to your project.

There are no existing patents, copyrights, and trademarks which are relevant to our project.

### Program/hardware design

#### Program details. What parts were tricky to write?

The skeleton of code we started from was unnecessarily complicated, so our biggest challenge was discovering how the actual control algorithms and peripheral interfaces were implemented. The code for the radio interface was reverse-engineered based on the serial string received from the controller. Although that was straightforward, it was far less straightforward to implement pitch, roll, and yaw control from the controller to the rest of the code. Hence, we stuck with throttle and arm only. From there, we scrapped his PID control as it was unfinished and did not work and wrote our own. This has its own challenges as we are implementing it on a quadcopter with four motors. One really important thing that we realized early was that the physical model for how yaw is controlled is extremely different from how pitch and roll is controlled. This means we needed separate coefficients for yaw PID and pitch/roll PID. To implement this, we simply compute the PID values, then apply the coefficients separately in the motor mixing calculations. 

The code has the following structure:

```
RotorRascals
├── main.c                  # Entry point, initializes hardware and control loop
├── lib/
│   ├── Control/
│   │   ├── controller.c       # PID controller logic
│   │   └── controller.h
│   ├── ESC/
│   │   ├── esc.c              # Motor PWM control
│   │   └── esc.h
│   ├── Filter/
│   │   ├── leaky_LP.c         # Leaky low-pass filter
│   │   └── leaky_LP.h
│   ├── MPU9250/
│   │   ├── mpu9250.c          # IMU driver
│   │   └── mpu9250.h
│   ├── Radio/
│   │   ├── radio.c            # UART radio input parsing
│   │   └── radio.h
├── include/
│   ├── config.h               # System-wide configuration macros
│   └── pins.h                 # Pin definitions
├── utils/
│   └── math_utils.c/h         # Optional math helpers
├── Makefile or CMakeLists    # Build system configuration
```
  
#### Hardware details -- Could someone else build this based on what you have written?

Below is a diagram showcasing the drone components

<p align="center">
    <img width="600" alt="DroneLabelled" src="figs/drone_labelled.jpg">
</p>

The drone 3D printable files can be found at the following link:
1. DIYDrone Frame with Motor Holes:
https://cad.onshape.com/documents/d38aab018657bf04d68fdbe6/w/71b7a7a8d496a9da1491b
bbb/e/70127a797fc69afda087e221
2. DIYDrone (Jackson & Dylan) Guards:
https://cad.onshape.com/documents/7de229710a5769dcea70d33f/w/a361731c2e298d2c6e283
035/e/6aab78923736995bb8b96cf8
3. DIYDrone Gimbal Interfacer:
https://cad.onshape.com/documents/8b853f430eb67b22ed155352/w/298f6506e82bca0de68ad
df6/e/2c12097c2bd1bacce00dfebb?renderMode=0&uiState=680d8b0e72d068090b086e62
4. Drone Gimbal Mount & Gimbal Drone Spacer:
https://cad.onshape.com/documents/e9f49230253cb2ceb026b522/w/5f775122fa42329de5afc46
6/e/d2d7e1e364a8d0e3d6eb7b74

The drone build guide can be found at the following links:
1. Frame 3D printing guide: https://github.com/cornellmotionstudio/DIYDrone/wiki/Frame
2. Guards 3D priting guide: https://github.com/cornellmotionstudio/DIYDrone/wiki/Guards
3. Mechanical assembly guide: https://github.com/cornellmotionstudio/QuadPopUp/wiki/Part-1:-Build-Your-Quadcopter 
4. Electrical assembly/Pico software guide: https://github.com/cornellmotionstudio/JacksonDronev2/wiki

To get up and running with the code and Pico build environment:

Clone the project
``` bash
git clone https://github.com/nekrutnikolai/RotorRascal.git
```

Go to the software dir.
``` bash
cd RotorRascal && cd software
```

Build the project
``` bash
./builder
```

Below is a bill of materials will all the parts necessary to build the drone and get up and running

Table 1: Drone components

| **Component**          | **Part Description**                                        | **Estimated Price**     |
|------------------------|-------------------------------------------------------------|--------------------------|
| **Frame**              | 3D Printed              | $3-5 in filament                  |
| **Motors**             | 2306 brushless motors, 1700–2500KV                 | $15–$30 each             |
| **ESC (4-in-1)**       | 4-in-1 ESC, 30–60A, DShot600 support                        | $40–$80                  |
| **Propellers**         | 3" tri-blade props                        | $3–$6 per set            |
| **Radio Receiver (Rx)**| ELRS protocol receiver                | $15–$30                  |
| **Battery**            | 4S LiPo, 1300–1800mAh                                 | $25–$50                  |

Table 2: Drone accessories

| **Component**         | **Part Description**                                         | **Estimated Price**     |
|------------------------|-------------------------------------------------------------|--------------------------|
| **Transmitter (Tx)**   | Radiomaster Boxer ELRS RC controller       | $100–$150                |
| **Battery Charger**    | LiPo balance charger (AC/DC or DC only)                    | $40–$100                 |

#### Be sure to specifically reference any design or code you used from someone else
    
We referenced the design and code found at the following repository: https://github.com/cornellmotionstudio/JacksonDronev2

### Tuning the drone PID's

Tuning the PIDs on a quadcopter is not the easiest task when it comes to a PID loop that you wrote yourself, on hardware that you also built yourself. It's especially difficult because there is no advice from other people that have experienced the issues. It boils down to a raw understanding of the physical model and the code at play. With our particular situation, the most difficult thing was trying to understand what was due to the gyro or if the issues were being caused by dynamics between the pitch and roll competing with yaw correction. Due to the physical model of a quadcopter, if a correction is made to any motor, it affects yaw, therefore inducing a yaw error that requires correction. Finding a starting point that did not have an oscillatory loop between the pitch/roll axis and the yaw axis was a challenge, but in the end, we were able to find a good starting point PIDs.

Table 3: Initial PID values

| Axis        | Kp  | Ki   | Kd    |
|-------------|-----|------|-------|
| Roll/Pitch  | 4.5 | 0.4  | 0.08  |
| Yaw         | 3.0 | 0.25 | 0.015 |

Continuing with our tuning, we were able to find really good PID values that resulted in an ability to react really well to outside stimuli, hold level quite well, and correct for new changes in center of gravity on the drone. The biggest challenge is finding a set of PIDs that works well through the whole throttle range; however, though this should not be the case unless we are amplifying noise or vibration caused by different throttle positions in our accelerometer measurements. Something we did still experience regardless of the amount of tuning done was a continuous angle error that varied with time; it almost appeared to be steady-state error that would then be corrected by the I term, which led us down some tuning rabbit holes in attempts to fix it. However, we believe now that it is actually drift occurring through our complimentary filter, and given more time, I believe we would have been able to correct this issue. Coming to this conclusion was based on testing with a solid offset that would need to be corrected by the I term, which did occur and namely occurred much faster than the correction to this drift. Additionally, testing our P term by tapping different points to induce an angle error and observing how quickly the drone corrected these errors indicated that whatever this drift was, it was being ignored by the PID loop. Testing PIDs was done purely on a custom-designed stand designed by Nikolai. The stand allows the drone to move along the pitch, roll, yaw, and vertical axis. Meaning we can simulate a hover that is restricted in translational movement. Using this stand with a power cable and a USB cable dangling from opposite ends of the drone, we are able to safely test indoors despite the high chance of unstable flight. 

Table 4: Final PID values that we found to work best:

| Axis        | Kp  | Ki   | Kd    |
|-------------|-----|------|-------|
| Roll/Pitch  | 20.0 | 2.0  | 1.5  |
| Yaw         | 3.0 | 0.25 | 0.015 |

### Results of the design

#### Any and all test data, scope traces, waveforms, etc

We tested the drone primarily on a custom-designed gimbal stand, which allowed safe experimentation of all rotational degrees of freedom (roll, pitch, yaw) and limited vertical movement. This setup enabled us to analyze hover stability, responsiveness, and overall system behavior under controlled conditions.

We evaluated PID response by:
- Applying known disturbances (e.g., tapping the frame) and observing response.
- Introducing sudden changes in throttle and analyzing recovery behavior.
- Monitoring gyro outputs in real time via serial logging.

Through this process, we fine-tuned the PID constants as shown in Table 4. 

Unfortunately, due to the 1kHz speed of the PID loop, we were unable to plot the data in real time without a noticeable reduction in performance.

#### Speed of execution (hesitation, flicker, interactiveness, concurrency)

The control loop was executed at **1 kHz**, which allowed timely updates and stable feedback. This speed was chosen based on earlier experimentation and provided a good balance between performance and computational headroom on the Pi Pico. There was **no observable hesitation or lag**, and the drone maintained high responsiveness to throttle changes and physical disturbances.

#### Accuracy (numeric, music frequencies, video signal timing, etc)

The orientation estimation was reasonably accurate for short time scales (under 2–3 minutes), with minor long-term drift observed due to IMU noise and limitations of the complementary filter. Control accuracy was sufficient to maintain a level hover and reject disturbances, though we noticed yaw compensation would lag slightly under certain conditions due to cross-axis coupling as a result of torque induced by the drone.

#### How you enforced safety in the design

Safety was a top priority throughout this project. The following measures were implemented:

- Test stand: Limited movement to safe ranges while enabling realistic control behavior.
- Throttle arm/disarm mechanism: Prevents accidental motor spin-up.
- Motor failsafe: If communication is lost, motors shut down immediately.
- Battery-powered only during flight: USB only used for debugging and PID tuning on the stand.
- PID zeroing on arm: Ensures no accumulated integral error before startup and prevents loss of fingers.
- Frame guards (optional): Optional frame propeller guards can be 3D printed and built into the frame 

These precautions allowed extensive indoor testing without risk to people or equipment.

#### Usability by you and other people

Our system was used by all group members and debugged collaboratively. Some key usability points include:

- Builder script simplified firmware compilation and flashing.
- Serial PID tuner allowed real-time adjustments, improving iteration speed.
- Open-source hardware/software make this project replicable by other students, especially within ECE/MAE courses.
- Documentation and GitHub Wiki were created to support further student use and integration.

### Conclusion

#### Analyse your design in terms of how the results met your expectations. What might you do differently next time?

Given more time, after a thorough analysis outlined in the section "background method", we would obtain the equations of motion and proceed to linearize them about the desired hover position. These would then guide us in our tuning process, by suggesting ranges of values for which the controller gains would result in a stable hovering equilibirum. In particular, the MATLAB controls toolbox would likely be vital in this process. We would then iterate over a loop of trial and tuning, until we obtain the desired response characteristics.   

#### How did your design conform to the applicable standards?

Our design aligns with several widely recognized engineering and safety standards:
- IEEE 21451: Our use of digital communication with the IMU and radio receiver follows the principles of this standard, which defines smart transducer interfaces for sensors and actuators. This is especially relevant to our modular use of sensors and potential expansion via Wi-Fi on the Pico W.
- IEEE 829 (Test Documentation Standard): While not formalized, we structured our testing methodology (PID tuning, safety checks, failure recovery) based on the principles of repeatable test planning and documentation.
- FCC Part 15: The 2.4 GHz radio communication between the transmitter and ELRS receiver operates in compliance with unlicensed ISM band regulations. Our system is entirely self-contained and avoids intentional emissions outside allowed frequencies.
- ISO 12100 (Safety of Machinery - Risk Assessment): We applied the principles of this standard by implementing startup gyro calibration, arm/disarm logic, and motor cutoff on communication loss. Testing on a gimbal stand further reduced risk during development.
- ISO 9001 (Quality Management): Though not formally certified, we adhered to quality principles including version-controlled development, structured documentation, and modular hardware/software integration to ensure maintainability and reproducibility for future coursework.

#### Intellectual property considerations

- **Did you reuse code or someone else's design?**
    Yes, we built up on top of code and hardware developed by the Motion Studio Drone Squad and Deemo Chen.

    The code and hardware that we built on top of can be found at the following GitHub repositories:
    - https://github.com/cornellmotionstudio/QuadPopUp
    - https://github.com/cornellmotionstudio/DIYDrone
    - https://github.com/cornellmotionstudio/JacksonDronev2
    
- **Did you use code in the public domain?**

    Yes, the Motion Studio GitHub repositories found above are public. 

- **Are you reverse-engineering a design? How did you deal with patent/trademark issues?**

    N/A, we are not reverse-engineering a design.

- **Did you have to sign non-disclosure to get a sample part?**

    No, we did not have to sign an NDA.

- **Are there patent opportunities for your project?**

    There are not patent opportunities for our project as most of this has already been dveloped and is available commerically.

### Appendix A (permissions)

The group ***approves*** this report for inclusion on the course website.

The group ***approves*** the video for inclusion on the course youtube channel.

### Additional appendices

- **Appendix with commented Verilog and/or program listings.**

Our commented code can be found at the following link in our repository: https://github.com/nekrutnikolai/RotorRascal/tree/master/software

- **Appendix with schematics if you build hardware external to the DE1-SoC board (you can download free software from expresspcb.com to draw schematics).**

<p align="center">
    <img width="600" alt="DroneSchematic" src="figs/schematic.png">
</p>

- **Appendix with a list of the specific tasks in the project carried out by each team member.**

    A list specific tasks is not appropriate for this project because we worked together on everything as a team. As a result, the contribution of each team is split roughly evenly.

- **References you used:**

    - Yaw compensation as implemented in BetaFlight (the most popular hobbyist flight software): https://betaflight.com/docs/wiki/guides/current/integrated-yaw
    - BetaFlight suggested tuning principles: https://betaflight.com/docs/wiki/guides/current/Freestyle-Tuning-Principles#suggested-setting-values-for-a-5-5
