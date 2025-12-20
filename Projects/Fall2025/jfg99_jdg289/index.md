---
title: DJ Mixing Board Final Project
layout: page
---

## Project Introduction

This project is a real-time embedded DJ mixing board that allows a user to mix two audio sources with adjustable volume and filtering using physical controls.
 
This project implements a real-time embedded DJ mixing board on the RP2040 microcontroller that mixes two audio channels with user-controlled gain, low-pass filtering, and high-pass behavior, while also supporting triggered playback of stored audio samples. The system processes audio in real time using ADC inputs, digital filtering, and DAC output, demonstrating core embedded systems concepts including concurrency, interrupt-driven timing, and hardware–software integration.

The primary goal of the project was to explore real-time audio signal processing in an embedded environment. Audio mixing was chosen because it is inherently timing-sensitive: even small deviations in sampling rate or processing latency immediately manifest as audible artifacts such as distortion, dropouts, or aliasing. By combining continuous analog inputs, digitally sampled control signals from potentiometers, and deterministic interrupt-based execution, this project provides a strong platform for studying real-time constraints, signal flow, and performance tradeoffs in embedded systems.


## High level design

The idea for this project was inspired by physical DJ controllers and audio mixers, which provide a familiar interface for manipulating sound using knobs and buttons. These systems map naturally to embedded design challenges such as strict timing requirements, concurrency between tasks, and user interaction through hardware controls. By implementing a DJ-style mixing board, the project emphasizes real-time performance while remaining intuitive to use and debug. Additionally, a key motivation was choosing a project that we were personally interested in and excited to build. This direct interest helped drive the project forward and resulted in a final system that was both functional and enjoyable to use during testing.

The system processes two independent audio channels using the RP2040’s ADC inputs on GPIO26 (ADC0) and GPIO28 (ADC2). Each channel has its own adjustable gain, low-pass filter strength, and high-pass cutoff. These parameters are controlled by six potentiometers connected to an external ADS7830 ADC, which is accessed over an I²C bus. Digitally processed audio samples are sent to an external dual-channel SPI DAC, which generates the final analog output.

At each interrupt of the high-speed repeating timer, the system reads the current ADC values, applies gain and filtering operations, optionally overrides the signal with stored audio samples, and writes the processed value to the DAC. As we are using monoaudio, each interrupt outputs a value to both the left and right speakers. The interrupt toggles which song it is outputting for the interrupt, resulting in the mixing of the two audio streams. A second timer runs at a much lower rate to update potentiometer values over I²C, decoupling slow control updates from the high-speed audio path. 


## Background Math

The mathematical foundation of the project includes linear gain scaling, first-order low-pass filtering, and amplitude limiting to approximate aggressive high-pass behavior. The low-pass filters are implemented using the standard recursive form y[n] = y[n−1] + a(x[n] − y[n−1]) where the coefficient a is dynamically adjusted using a potentiometer input. 

## Hardware/Software Tradeoffs

This project uses both hardware and software filtering to balance signal integrity and flexibility. Our implementation of a hardware filter includes a voltage biasing dc blocking filter that cleans up the input audio to only contain a clean ac input from the 3.5mm audio jack, while biasing our waveform around 1.65V as to keep our ADC inputs within 0-3.3V.

<img width="841" height="294" alt="Screenshot 2025-12-19 at 7 56 26 PM" src="https://github.com/user-attachments/assets/0abe601d-4c20-4f68-842c-6aae6481cf13" />

*Figure 1: Protoboard Schematic*

Our software filters allow for adjustability and control over the effects we want to impose on our sampled audio. Though the inputs are analog, we get full control over how we interpret those values of 0-3.3V, to drive our digital low-pass and high-pass filters.

```c
adc_select_input(0);               // select ADC0 (GPIO26)
uint16_t raw0 = adc_read();        
DAC_output_0 = (uint16_t)(raw0 * level_a); // scale by level_a
float alpha_l = 0.02f + lp_a * 0.4f;  //low pass
float x = (float)raw0;
y_a = y_a + alpha_l * (x - y_a);
```
*Applying scaling and filters to output*

Initially, the project envisioned streaming audio from a computer over the RP2040’s USB interface, eliminating the need for discrete analog audio inputs. This idea was reflected in the original PCB design, which does not directly connect 3.5 mm audio jacks to the RP2040’s ADC pins. However, it quickly became clear that implementing real-time USB audio streaming would be significantly more complex and time-consuming than building the inputs out in hardware, which we did using a protoboard that connected to the broken-out ADC inputs

<img width="600" alt="Screenshot 2025-12-19 at 7 45 27 PM" src="https://github.com/user-attachments/assets/844c2762-d327-435f-b462-343ac22ba728" />

*Figure 2: Protoboard*

## Intellectual property considerations

The project does not infringe on existing patents, trademarks, or copyrighted designs. Audio samples used during testing were sourced from open platforms and were used strictly for educational purposes. No monetization is involved, and the use of audio content falls under fair use guidelines for academic and non-commercial projects.

## Program/hardware design
At a high level, audio flows continuously from the analog inputs to a voltage bias filter, through ADC sampling and digital processing, to the DAC output, forming the core data path of the system. For the digital processing, user inputs and configuration updates are handled in parallel by slower control logic that adjusts parameters without interrupting audio playback. This organization allows the system to remain responsive and interactive while preserving consistent audio performance.

### Software
The software architecture is structured around two repeating timers. The primary timer runs at approximately 45 kHz and executes the sample_and_output_cb function, which performs all real-time audio processing. This sampling rate was chosen based on the Nyquist sampling theorem, which states that a signal must be sampled at least twice its highest frequency component to avoid aliasing. Since human hearing extends up to approximately 20 kHz, a sampling rate above 40 kHz ensures accurate reconstruction of the audible spectrum. 

Within each interrupt, the function reads both ADC channels, applies gain scaling and filtering, manages playback triggers, and writes the resulting value to the DAC. Gain is applied as a linear scaling factor derived from the potentiometer input, while low-pass filtering is implemented using a first-order low-pass filter. The filter coefficient alpha is dynamically adjusted based on a potentiometer value, giving the user real-time control over the effective cutoff frequency.


```c
float alpha = 0.02f + lp_a * 0.4f;
y_a = y_a + alpha * (x - y_a);
float out = y_a * level_a;
```
*Low pass filter*

Our high-pass filter functions as a frequency cutoff by not playing certain frequencies based on a potentiometer value. This resulted in a cool audio effect that we chose to keep. 

To achieve mixing behavior without, the system alternates between processing the two audio channels on successive interrupts. A toggle variable determines which channel is processed on each invocation, effectively interweaving samples from the two sources at the DAC. Because the interrupt rate is high, this produces a perceptually smooth mix without audible artifacts. The processed sample is then transmitted to the external dual-channel DAC over SPI. SPI communication was chosen for its high throughput and deterministic timing compared to other serial protocols.

A second repeating timer, running at a much lower rate (every 10 ms), executes the read_pot_cb function. This callback reads six potentiometers via an external ADS7830 ADC over I²C. These potentiometers control gain, low-pass strength, and high-pass cutoff for both audio channels.


```c
if (i2c_write_blocking(I2C_CHAN, ADDRESS, &cmd_lp_a, 1, true) < 0)
    return true;
i2c_read_blocking(I2C_CHAN, ADDRESS, &pot_val_lp_a, 1, false);
lp_a = (float)pot_val_lp_a / 255.0f;
```

*Example pot reading*

The filters, particularly the highpass, were the most challenging aspect to implement correctly without complete audio loss. We also initially struggled with timing, as if we attempted to do too much logic in the read and write timer, then we would not meet timing requirements for the DAC resulting in no sound playing. This is when we introduced a slower timer to handle the readings of the potentiometers. 

### Hardware
The hardware consists of an RP2040 microcontroller, two analog audio inputs, an external ADS7830 ADC for potentiometer readings, and a dual-channel SPI DAC for audio output. GPIO pins are clearly defined for SPI, I²C, ADC, and digital input, and the code documents all pin assignments and communication protocols. A custom PCB designed in Altium was used for the main system, while the analog filters and audio jacks were implemented on a protoboard. 

The pcb was placed in the enclosure and the lid was placed over it. Knobs were then placed on the potentiometers for better grip. One computer was connected to the RP2040 and the debugger from which the board was powered and flashed. The two audio inputs were then connected to computers which independently stream songs into. The audio output was then connected to the speaker. An oscilloscope was also connected to the ground pin on the protoboard, as this helped reduce noise on the sounds caused by the isolated computer grounds being different than the speaker ground.

<img width="600" alt="Screenshot 2025-12-19 at 7 59 55 PM" src="https://github.com/user-attachments/assets/40adf742-a9fc-491d-9367-c9d235c94af3" />

*Figure 3: Custom PCB and Protoboard Schematic*


<img width="600" alt="Screenshot 2025-12-19 at 8 04 27 PM" src="https://github.com/user-attachments/assets/b8ee368a-e1f2-4f15-8f74-5ca3c67940ee" />

*Figure 4: Custom PCB*


<img width="600"  alt="Screenshot 2025-12-19 at 8 05 02 PM" src="https://github.com/user-attachments/assets/958d3604-203c-4bf4-8377-29806b77ed12" />

*Figure 5:Integrated DJ Mixing Board*

<img width="600" alt="Screenshot 2025-12-19 at 8 24 59 PM" src="https://github.com/user-attachments/assets/fa283917-82e2-4997-8910-0a33c5831710" />

*Figure 6: Full DJ Setup*


<audio controls preload="none">
  <source src="StarshipsXOneMoreTime.m4a" type="audio/mpeg">
  Your browser does not support the audio element.
</audio>

*Mix 1: Starships X One More Time*

<audio controls preload="none">
  <source src="CornellUniversity12.m4a" type="audio/mpeg">
  Your browser does not support the audio element.
</audio>

*Mix 2: Blow x Big Time Rush*


Several features were considered but ultimately not implemented, including advanced playback control, reverb effects, and BPM adjustment. These features would require significantly more memory than is available on the RP2040, as well as the addition of external memory hardware. Given time and hardware constraints, these ideas were deferred in favor of ensuring stable real-time audio performance.

AI tools were used selectively to assist with debugging and troubleshooting during development. Lines in code generated in AI are commented with “ // AI Help “.

## Results of the design

<img width="600" alt="Screenshot 2025-12-19 at 8 00 58 PM" src="https://github.com/user-attachments/assets/81d3c4a9-0a48-4979-9e54-8f3eefca2557" />

*Figure 6:Pre-1.65V Bias Audio Input*

<img width="600" alt="Screenshot 2025-12-19 at 8 02 07 PM" src="https://github.com/user-attachments/assets/b46f080d-25e0-4fc9-a9dd-1e324bb5f479" />

*Figure 7: Post-1.65V Bias Audio Input*

The system executes fast enough to meet all real-time constraints. No audible hesitation, flicker, or dropouts were observed during operation. Alternating SPI writes between DAC channels proved sufficient to maintain consistent output timing and audio quality. The output audio sounded clear and clean, with no noticeable distortion or degradation. In many cases, it was difficult to tell that the signal was being processed through an embedded system, indicating accurate sampling, filtering, and reconstruction.

The PCB is enclosed in a plastic housing, preventing users from touching exposed circuitry during operation. This enclosure improves both electrical safety and overall robustness. The knobs on the potentiometers also make it easy to interact with. The system is intuitive and easy to use. Multiple users were able to interact with the mixing board without instruction beyond a brief explanation of each knob’s function, demonstrating effective user-centered design.

## Conclusions

The project successfully met its goals by implementing a stable, real-time embedded DJ mixing board with interactive physical controls and high-quality audio output. The system performed as expected under real-time constraints and demonstrated effective hardware/software integration. In future iterations, adding external memory would enable advanced features such as BPM adjustment, pitch shifting, reverb, echo, and playback control, bringing the system closer to a commercial DJ controller.

The design conforms to applicable standards by using a custom PCB instead of a breadboard, improving reliability and professionalism. The addition of an enclosure further enhances safety and usability.

## Appendix A
The group approves this report for inclusion on the course website.
The group approves the video for inclusion on the course youtube channel.

## Appendix B

### Tasks
Low Pass Filter Code, ADC Mux Reading Code, Timing Code - Jolene
Stored Playback Code, PCB Design, High Pass Filter Code  - Jacob

### Links to datasheets
ADS7830IPWR - https://www.ti.com/general/docs/suppproductinfo.tsp?distId=10&gotoUrl=https%3A%2F%2Fwww.ti.com%2Flit%2Fgpn%2Fads7830
MCP4822-E/SN - https://ww1.microchip.com/downloads/aemDocuments/documents/OTH/ProductDocuments/DataSheets/20002249B.pdf

### Schematics
<img width="600" alt="Screenshot 2025-12-19 at 7 59 55 PM" src="https://github.com/user-attachments/assets/40adf742-a9fc-491d-9367-c9d235c94af3" />

### Code
```c
**
 * Audio-through x2: 
 *   ADC0 (GPIO26)  
 *   ADC2 (GPIO28)  
 */

#include "vga16_graphics_v2.h"

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "hardware/dma.h"
#include "hardware/spi.h"

#include "pico/stdlib.h"
#include "pico/divider.h"
#include "pico/multicore.h"

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/adc.h"
#include "hardware/pll.h"

#include "pt_cornell_rp2040_v1_4.h"
#include "hardware/i2c.h"

#include "audio_data_40k.h"   // the header for DJ Tag
#include "audio_data_40k_9.h"   // new sound


// DJ Tag variables
volatile uint32_t audio_idx = 0;
volatile bool audio_playing = false;
volatile bool last_gp2_state = false;   // check for press

//hunter name drop variables
volatile uint32_t audio_idx_b = 0;
volatile bool audio_playing_b = false;
volatile bool last_gp3_state = false;   // check for press

// --- Pin and timing definitions ---
#define ADC0_GPIO        26      // GPIO26 = ADC0
#define ADC2_GPIO        28      // GPIO28 = ADC2

#define SAMPLE_RATE_HZ   45000   // timer callbacks per second


// SPI data
uint16_t DAC_data_1; // output value
uint16_t DAC_data_0; // output value

//values for DAC
int DAC_output_0 ;
int DAC_output_1 ;


// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000

// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

// SPI configurations (note these represent GPIO number, NOT pin number)
#define PIN_CS 5
#define PIN_SCK 6
#define PIN_MOSI 7
#define SPI_PORT spi0

#define I2C_CHAN i2c1
#define I2C_BAUD_RATE 400000
#define ADDRESS 0x48


// alarm infrastructure 
#define ALARM_NUM 0
#define ALARM_IRQ TIMER_IRQ_0
 
//potentiometer stuff
float level_a;
float level_b;
float lp_a = 0.5f;      // 0..1 from the low-pass pot (R1)
float lp_b = 0.5f;      // 0..1 from the low-pass pot (R4)
float hp_a = 1.0f;      // high-pass pot (R2)
float hp_b = 1.0f;      // high-pass pot (R5)
static float y_a = 0.0f; // filter state for channel A
static float y_b = 0.0f; // filter state for channel B

static repeating_timer_t sample_timer;
static uint slice0, slice1;

bool read_pot_cb(repeating_timer_t *rt) {

    uint8_t cmd_a = 0x8C;  // ADS7830 command for channel 0 (G_A)
    uint8_t pot_val_a = 0;
    static bool toggleab = false;

// ---------------------------------------A stuff ---------------------------------------------
 
    if (i2c_write_blocking(I2C_CHAN, ADDRESS, &cmd_a, 1, true) < 0)
        return true;
    i2c_read_blocking(I2C_CHAN, ADDRESS, &pot_val_a, 1, false);
    level_a = (float)pot_val_a / 255.0f; //a gain

    
    uint8_t cmd_lp_a = 0xCC;  // ADS7830 command for channel 1 (LP_A)
    uint8_t pot_val_lp_a = 0;
    const float alpha_a = 0.1f;

    if (i2c_write_blocking(I2C_CHAN, ADDRESS, &cmd_lp_a, 1, true) < 0)
        return true;
    i2c_read_blocking(I2C_CHAN, ADDRESS, &pot_val_lp_a, 1, false);
    lp_a = (float)pot_val_lp_a / 255.0f;

    uint8_t cmd_hp_a = 0x9C;   // CH2 (high pass A)
    uint8_t pot_val_hp_a = 0;
    if (i2c_write_blocking(I2C_CHAN, ADDRESS, &cmd_hp_a, 1, true) < 0)
        return true;
    i2c_read_blocking(I2C_CHAN, ADDRESS, &pot_val_hp_a, 1, false);
    hp_a = (float)pot_val_hp_a / 255.0f;

   

// --------------------------------------- B stuff ---------------------------------------------
    
    uint8_t cmd_b = 0xFC;  // ADS7830 command for channel 7 (G_B)
    uint8_t pot_val_b = 0;
    if (i2c_write_blocking(I2C_CHAN, ADDRESS, &cmd_b, 1, true) < 0)
        return true;
    i2c_read_blocking(I2C_CHAN, ADDRESS, &pot_val_b, 1, false);
    level_b = (float)pot_val_b / 255.0f; // b gain

    
    uint8_t cmd_lp_b = 0xAC;  // ADS7830 command for channel 4 (LP_B)
    uint8_t pot_val_lp_b = 0;
    const float alpha_b = 0.1f;

    if (i2c_write_blocking(I2C_CHAN, ADDRESS, &cmd_lp_b, 1, true) < 0)
        return true;
    i2c_read_blocking(I2C_CHAN, ADDRESS, &pot_val_lp_b, 1, false);
    lp_b = (float)pot_val_lp_b / 255.0f;
    
    uint8_t cmd_hp_b = 0xEC;   // CH5 (high pass B)
    uint8_t pot_val_hp_b = 0;
    if (i2c_write_blocking(I2C_CHAN, ADDRESS, &cmd_hp_b, 1, true) < 0)
        return true;
    i2c_read_blocking(I2C_CHAN, ADDRESS, &pot_val_hp_b, 1, false);
    hp_b = (float)pot_val_hp_b / 255.0f;


    return true;
}


bool sample_and_output_cb(repeating_timer_t *rt) {

    //clearing alarm
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM); 

    // GP2 triggering stuff
    const uint PLAY_PIN = 2;
    bool gp2_now = gpio_get(PLAY_PIN);

    // checks for rising edge (lets us spam button without restarting sound)
    if (gp2_now && !last_gp2_state) {
        audio_idx = 0;
        audio_playing = true;
    }

    // stops playback
    if (!gp2_now) {
        audio_playing = false;
    }

    last_gp2_state = gp2_now;

    //SONG A CODE

    
    adc_select_input(0);               // select ADC0 (GPIO26)
    uint16_t raw0 = adc_read();        
    DAC_output_0 = (uint16_t)(raw0 * level_a); // scale by level_a
    
    float alpha_l = 0.02f + lp_a * 0.4f;  //low pass
    float x = (float)raw0;
    y_a = y_a + alpha_l * (x - y_a);

 
    
    //high pass
 
    //gain
    float out = y_a * level_a;

    
    float cutoff = 4095.0f * hp_a;

    if (out < 0) out = 0;
    //if (out > 4095.0f) out = 4095.0f;
    if (out > cutoff) out = cutoff;

    DAC_output_0 = (uint16_t)out;

    // override output for button press
    if (audio_playing && gp2_now) {
        if (audio_idx < AUDIO_DATA_LEN) {
            DAC_output_0 = audio_data[audio_idx] & 0x0FFF; 
            audio_idx++;
        } else {
            // stops at end 
            audio_playing = false;
        }
    }

    DAC_data_0 = (DAC_config_chan_A | (DAC_output_0 & 0xffff));

    //song b code
    adc_select_input(2);               // select ADC2 (GPIO28)
    uint16_t raw1 = adc_read();
    DAC_output_1 = (uint16_t)(raw1 * level_b); // scale by level_b

    float alpha_b = 0.02f + lp_b * 0.4f;  //low pass
    float x_b = (float)raw1;
    y_b = y_b + alpha_b * (x_b - y_b);
    float out_b = y_b * level_b;
    
    //high pass



    float cutoff_b = 4095.0f * hp_b;

    if (out_b < 0) out_b = 0;
    //if (out_b > cutoff) out_b = cutoff;
    if (out_b > cutoff_b) out_b = cutoff_b;
    
    DAC_output_1 = (uint16_t)out_b;

    DAC_data_1 = (DAC_config_chan_B | (DAC_output_1 & 0xffff));

    static bool toggle = false;

    if (toggle) {

        spi_write16_blocking(SPI_PORT, &DAC_data_0, 1);
    }
     else {
        spi_write16_blocking(SPI_PORT, &DAC_data_1, 1);
     }

    toggle = !toggle;   // flip it for next time

    return true;
}


int main() {
    stdio_init_all();
    spi_init(SPI_PORT, 200000000) ;
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;
    
    i2c_init(I2C_CHAN, I2C_BAUD_RATE);
    gpio_set_function(14, GPIO_FUNC_I2C);
    gpio_set_function(15, GPIO_FUNC_I2C);

    adc_init();
    adc_gpio_init(ADC0_GPIO);      // prepare pin for ADC0
    adc_gpio_init(ADC2_GPIO);      // prepare pin for ADC2

    //setting rate for ADC
    adc_set_clkdiv(2399.0f);

    //--AI--HELP for better timing of read
    int us = -(1000000 / SAMPLE_RATE_HZ);
    add_repeating_timer_us(us, sample_and_output_cb, NULL, &sample_timer);
    // reading for potentiometer
    static repeating_timer_t pot_timer;
    add_repeating_timer_ms(10, read_pot_cb, NULL, &pot_timer);

    const uint PLAY_PIN = 2;
    gpio_init(PLAY_PIN);
    gpio_set_dir(PLAY_PIN, GPIO_IN);
    gpio_pull_down(PLAY_PIN);   // keeps it low if floating


    while (true) {
        tight_loop_contents(); // all work done in interrupt
    }
}
```

