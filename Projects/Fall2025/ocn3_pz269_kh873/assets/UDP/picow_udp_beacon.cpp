/**
 * Copyright (c) 2022 Andrew McDonnell
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Standard libraries
#include <math.h>
#include <string.h>

#include <iostream>

#include "pico/sync.h"
// LWIP libraries
#include "lwip/pbuf.h"
#include "lwip/udp.h"
// Pico SDK hardware support libraries
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/timer.h"
// Our header for making WiFi connection
#include "connect.h"
#include "hardware/spi.h"
// Protothreads
#include "pt_cornell_rp2040_v1_4.h"
// Circular queue for buffering
#include "CircularQueue.hpp"

// Destination port and IP address
#define UDP_PORT 1234
#define BEACON_TARGET "172.20.10.10"

// Maximum length of our message
#define BEACON_MSG_LEN_MAX 1024

// DAC parameters (MCP4822 datasheet)
// Bit 15: 0=Channel A, 1=Channel B
// Bit 14: 0 (ignored)
// Bit 13: 1=1x gain, 0=2x gain
// Bit 12: 1=Active, 0=Shutdown
// Bits 11-0: 12-bit data value
// A-channel, 1x gain, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x gain, active  
#define DAC_config_chan_B 0b1011000000000000

// SPI data
uint16_t DAC_data_1;  // output value
uint16_t DAC_data_0;  // output value

// SPI configurations (note these represent GPIO number, NOT pin number)
#define PIN_MISO 4
#define PIN_CS 5
#define PIN_SCK 6
#define PIN_MOSI 7
#define LDAC_PIN 8
#define SPI_PORT spi0

#define ALARM_IRQ TIMER_IRQ_0

constexpr int ALARM_NUM = 0;

constexpr int SAMPLE_RATE = 44100;

constexpr int DELAY = 1000000 / SAMPLE_RATE;

// GPIO for timing the ISR
#define ISR_GPIO 2

// GPIO for mode switching button (with pull-up, active LOW)
#define BUTTON_GPIO 3

// GPIO output to indicate TX mode (HIGH=TX, LOW=RX)
#define MODE_INDICATOR_GPIO 9

// ADC input for microphone (GPIO 26 = ADC0, GPIO 27 = ADC1, GPIO 28 = ADC2)
#define MIC_ADC_GPIO 26
#define MIC_ADC_CHANNEL 0

// DMA channel for SPI
int dma_channel;

// Protocol control block for UDP receive connection
static struct udp_pcb* udp_rx_pcb;

// Circular buffer for audio samples (8192 samples total = ~185ms at 44.1kHz stereo)
CircularQueue<uint16_t, 8192> audio_buffer;

// Temporary receive buffer
char data_buffer[BEACON_MSG_LEN_MAX];

// Track if we have enough samples to start playing
volatile bool playing = false;
volatile int samples_buffered = 0;

// Bandwidth measurement
volatile uint32_t bytes_received = 0;
volatile uint32_t last_measurement_time = 0;

// Mode tracking: true = TRANSMIT, false = RECEIVE
volatile bool tx_mode = false;

// Transmit buffer for audio samples (collect samples before sending)
#define TX_BUFFER_SIZE 1024  // Send 1024 samples per UDP packet = 512 stereo pairs (~11.6ms at 44.1kHz stereo)
uint16_t tx_buffer_a[TX_BUFFER_SIZE];
uint16_t tx_buffer_b[TX_BUFFER_SIZE];
uint16_t* tx_write_buffer = tx_buffer_a;  // Buffer being written to by ISR
uint16_t* tx_send_buffer = tx_buffer_b;   // Buffer ready to send
volatile int tx_buffer_index = 0;
volatile bool tx_buffer_ready = false;

// Next alarm time (absolute)
static uint32_t next_alarm_time = 0;

// Semaphore for signaling a new received message
semaphore_t new_message;

// Protocol control block for UDP transmit connection
static struct udp_pcb* udp_tx_pcb;
static ip_addr_t tx_target_addr;

// Low-pass filter for microphone input (reduces noise)
// Using exponential moving average: y[n] = alpha * x[n] + (1-alpha) * y[n-1]
// Alpha = 0.4 gives cutoff around 5-6kHz at 44.1kHz sample rate (preserves voice clarity)
static float lpf_state = 2048.0f;  // Initialize to mid-range (12-bit ADC center)
static constexpr float LPF_ALPHA = 0.4f;

// Button interrupt handler - toggles between TX and RX modes
void button_gpio_callback(uint gpio, uint32_t events) {
    // Debounce: ignore if button pressed too quickly
    static absolute_time_t last_press_time = {0};
    absolute_time_t now = get_absolute_time();
    
    if (absolute_time_diff_us(last_press_time, now) < 200000) {  // 200ms debounce
        return;
    }
    last_press_time = now;
    
    // Toggle mode
    tx_mode = !tx_mode;
    
    // Update mode indicator GPIO
    gpio_put(MODE_INDICATOR_GPIO, tx_mode ? 1 : 0);
    
    // Reset state when switching modes
    if (tx_mode) {
        // Switching to TRANSMIT mode
        printf("Mode: TRANSMIT (sending audio from microphone)\n");
        playing = false;
        samples_buffered = 0;
        tx_buffer_index = 0;
        tx_buffer_ready = false;
    } else {
        // Switching to RECEIVE mode
        printf("Mode: RECEIVE (playing audio from network)\n");
        tx_buffer_index = 0;
        tx_buffer_ready = false;
    }
}

static void udpecho_raw_recv(void* arg, struct udp_pcb* upcb, struct pbuf* p, const ip_addr_t* addr,
                             u16_t port) {
    LWIP_UNUSED_ARG(arg);

    // Check that there's something in the pbuf
    if (p != NULL) {
        // Copy the contents of the payload (use actual packet length)
        size_t copy_len = (p->len < BEACON_MSG_LEN_MAX) ? p->len : BEACON_MSG_LEN_MAX;
        memcpy(data_buffer, p->payload, copy_len);
        
        // Track bytes received for bandwidth calculation
        bytes_received += copy_len;
        
        // Enqueue samples into circular buffer
        uint16_t* samples = (uint16_t*)data_buffer;
        int num_samples = copy_len / 2;  // Convert bytes to 16-bit samples
        
        // Debug: print first few samples from first packet to check data format
      //  static bool first_packet = true;
       // if (first_packet && num_samples > 0) {
       //     printf("First packet samples (hex): [0]=0x%04x [1]=0x%04x [2]=0x%04x [3]=0x%04x\n",
        //           samples[0], samples[1], samples[2], samples[3]);
        //    printf("First packet samples (dec): [0]=%u [1]=%u [2]=%u [3]=%u\n",
        //           samples[0], samples[1], samples[2], samples[3]);
        //    first_packet = false;
      //  }
        
        for (int i = 0; i < num_samples; i++) {
            if (!audio_buffer.enQueue(samples[i])) {
                // Buffer full - this is actually good, means we have data
                break;
            }
            samples_buffered++;
        }
        
        // Start playing once we have enough samples buffered (larger buffer = smoother playback)
        if (!playing && samples_buffered >= 512) {
            playing = true;
       //     printf("Starting playback with %d samples buffered\n", samples_buffered);
        }

        // Semaphore-signal a thread
        PT_SEM_SDK_SIGNAL(pt, &new_message);
        // Free the PBUF
        pbuf_free(p);
    } else {
        printf("NULL pt in callback");
    }
}

// ===================================
// Define the recv callback
void udpecho_raw_init(void) {
    // Initialize the RX protocol control block
    udp_rx_pcb = udp_new_ip_type(IPADDR_TYPE_ANY);

    // Make certain that the pcb has initialized, else print a message
    if (udp_rx_pcb != NULL) {
        // Err_t object for error codes
        err_t err;
        // Bind this PCB to our assigned IP, and our chosen port. Received messages
        // to this port will be directed to our bound protocol control block.
        err = udp_bind(udp_rx_pcb, netif_ip_addr4(netif_default), UDP_PORT);
        // Check that the bind was successful, else print a message
        if (err == ERR_OK) {
            // Setup the receive callback function
            udp_recv(udp_rx_pcb, udpecho_raw_recv, NULL);
        } else {
            printf("bind error");
        }
    } else {
        printf("udp_rx_pcb error");
    }
}

// This timer ISR is called on core 0
static void alarm_irq(void) {
    // Toggle ISR_GPIO to verify ISR is running (can see on oscilloscope)
    gpio_put(ISR_GPIO, 1);
    
    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // Calculate next alarm time
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY;

    // Handle transmit mode (sampling microphone)
    if (tx_mode) {
        // Read ADC value from microphone (12-bit value)
        uint16_t adc_sample = adc_read();
        
        // Apply low-pass filter to reduce high-frequency noise
        lpf_state = LPF_ALPHA * (float)adc_sample + (1.0f - LPF_ALPHA) * lpf_state;
        uint16_t filtered_sample = (uint16_t)lpf_state;
        
        // Store in transmit buffer as stereo (duplicate mono sample for L and R)
        tx_write_buffer[tx_buffer_index++] = filtered_sample;  // Left channel
        tx_write_buffer[tx_buffer_index++] = filtered_sample;  // Right channel
        
        // When buffer is full, swap buffers
        if (tx_buffer_index >= TX_BUFFER_SIZE) {
            // Swap write and send buffers
            uint16_t* temp = tx_send_buffer;
            tx_send_buffer = tx_write_buffer;
            tx_write_buffer = temp;
            
            // Reset index and signal ready
            tx_buffer_index = 0;
            tx_buffer_ready = true;
        }
        
        gpio_put(ISR_GPIO, 0);
        return;
    }

    // Handle receive mode (playing audio through DAC)
    // Check if we should be playing
    if (!playing) {
        gpio_put(ISR_GPIO, 0);
        return;
    }

    // Dequeue two samples for stereo (left and right channels)
    auto sample_L = audio_buffer.deQueue();
    auto sample_R = audio_buffer.deQueue();
    
    if (!sample_L.has_value() || !sample_R.has_value()) {
        // Buffer underrun - no data available
        playing = false;
        samples_buffered = 0;
        gpio_put(ISR_GPIO, 0);
        return;
    }
    
    samples_buffered -= 2;

    // Send left to channel A, right to channel B (or vice versa depending on your wiring)
    DAC_data_0 = (DAC_config_chan_A | (sample_L.value() & 0x0fff));
    DAC_data_1 = (DAC_config_chan_B | (sample_R.value() & 0x0fff));

    // Write to SPI (data goes into DAC input registers)
    spi_write16_blocking(SPI_PORT, &DAC_data_0, 1);
    spi_write16_blocking(SPI_PORT, &DAC_data_1, 1);
    
    // Pulse LDAC LOW to latch data from input registers to output
    gpio_put(LDAC_PIN, 0);
    gpio_put(LDAC_PIN, 1);
    
    // Clear ISR_GPIO at end
    gpio_put(ISR_GPIO, 0);
}

static PT_THREAD(protothread_receive(struct pt* pt)) {
    //  Begin thread
    PT_BEGIN(pt);

    while (1) {
        // Wait on a semaphore
        PT_SEM_SDK_WAIT(pt, &new_message);

        // Measure bandwidth every second
        uint32_t current_time = to_ms_since_boot(get_absolute_time());
        if (last_measurement_time == 0) {
            last_measurement_time = current_time;
        }
        
        uint32_t elapsed = current_time - last_measurement_time;
        if (elapsed >= 1000) {  // Every 1 second
            float bytes_per_sec = (float)bytes_received / (elapsed / 1000.0f);
            float kbps = (bytes_per_sec * 8.0f) / 1000.0f;  // Convert to kilobits per second
            
            if (bytes_received > 0) {  // Only print if receiving data
                printf("RX: %.2f KB/s (%.2f kbps), %u bytes in %u ms\n", 
                       bytes_per_sec / 1024.0f, kbps, bytes_received, elapsed);
            }
            
            // Reset counters
            bytes_received = 0;
            last_measurement_time = current_time;
        }

        PT_YIELD_usec(22);
    }

    // End thread
    PT_END(pt);
}


static PT_THREAD(protothread_send(struct pt* pt)) {
    // Begin thread
    PT_BEGIN(pt);

    // Make a static local UDP protocol control block
    static struct udp_pcb* pcb;
    // Initialize that protocol control block
    pcb = udp_new();

    // Create a static local IP_ADDR_T object
    static ip_addr_t addr;
    // Set the value of this IP address object to our destination IP address
    ipaddr_aton(BEACON_TARGET, &addr);

    while (1) {
        // Prompt the user
        sprintf(pt_serial_out_buffer, "> ");
        serial_write;

        // Perform a non-blocking serial read for a string
        serial_read;

        // Allocate a PBUF
        struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, BEACON_MSG_LEN_MAX + 1, PBUF_RAM);

        // Pointer to the payload of the pbuf
        char* req = (char*)p->payload;

        // Clear the pbuf payload
        memset(req, 0, BEACON_MSG_LEN_MAX + 1);

        // Fill the pbuf payload
        snprintf(req, BEACON_MSG_LEN_MAX, "%s: %s \n", ip4addr_ntoa(netif_ip_addr4(netif_default)),
                 pt_serial_in_buffer);

        // Send the UDP packet
        err_t er = udp_sendto(pcb, p, &addr, UDP_PORT);

        // Free the PBUF
        pbuf_free(p);

        // Check for errors
        if (er != ERR_OK) {
            printf("Failed to send UDP packet! error=%d", er);
        }
    }
    // End thread
    PT_END(pt);
}

// New thread for transmitting audio data when in TX mode
static PT_THREAD(protothread_tx_audio(struct pt* pt)) {
    PT_BEGIN(pt);

    static int packets_sent = 0;
    
    while (1) {
        // Only send when in TX mode and buffer is ready
        if (tx_mode && tx_buffer_ready) {
            // Allocate a pbuf for the audio data
            struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, TX_BUFFER_SIZE * sizeof(uint16_t), PBUF_RAM);
            
            if (p != NULL) {
                // Copy audio data from the send buffer (ISR is writing to the other buffer)
                memcpy(p->payload, tx_send_buffer, TX_BUFFER_SIZE * sizeof(uint16_t));
                
                // Send the UDP packet
                err_t er = udp_sendto(udp_tx_pcb, p, &tx_target_addr, UDP_PORT);
                
                // Free the PBUF
                pbuf_free(p);
                
                if (er != ERR_OK) {
                    printf("Failed to send audio packet! error=%d\n", er);
                } else {
                    packets_sent++;
                    if (packets_sent % 100 == 0) {
                        printf("TX: Sent %d packets\n", packets_sent);
                    }
                }
            } else {
                printf("Failed to allocate pbuf for TX\n");
            }
            
            // Clear the ready flag (buffer swap already happened in ISR)
            tx_buffer_ready = false;
        } else if (!tx_mode) {
            // Reset when not in TX mode
            packets_sent = 0;
        }
        
        // Yield to allow other threads to run (check every 1ms for responsiveness)
        PT_YIELD_usec(1000);
    }
    
    PT_END(pt);
}

int main() {
    // Initialize stdio
    stdio_init_all();

    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000);
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, spi_cpol_t(0), spi_cpha_t(0), spi_order_t(0));

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI);

    // Map LDAC pin to GPIO port, initialize HIGH (pulse LOW to latch)
    gpio_init(LDAC_PIN);
    gpio_set_dir(LDAC_PIN, GPIO_OUT);
    gpio_put(LDAC_PIN, 1);  // Start HIGH, will pulse LOW to latch

    // Setup the ISR-timing GPIO
    gpio_init(ISR_GPIO);
    gpio_set_dir(ISR_GPIO, GPIO_OUT);
    gpio_put(ISR_GPIO, 0);

    // Setup the mode indicator GPIO (HIGH=TX, LOW=RX)
    gpio_init(MODE_INDICATOR_GPIO);
    gpio_set_dir(MODE_INDICATOR_GPIO, GPIO_OUT);
    gpio_put(MODE_INDICATOR_GPIO, 0);  // Start in RX mode

    // Setup button GPIO with pull-up (button press pulls to ground)
    gpio_init(BUTTON_GPIO);
    gpio_set_dir(BUTTON_GPIO, GPIO_IN);
    gpio_pull_up(BUTTON_GPIO);
    
    // Enable interrupt on button press (falling edge)
    gpio_set_irq_enabled_with_callback(BUTTON_GPIO, GPIO_IRQ_EDGE_FALL, true, &button_gpio_callback);

    // Initialize ADC for microphone input
    adc_init();
    adc_gpio_init(MIC_ADC_GPIO);
    adc_select_input(MIC_ADC_CHANNEL);

    // Connect to WiFi
    if (connectWifi(country, WIFI_SSID, WIFI_PASSWORD, auth)) {
        printf("Failed connection.\n");
    }

    // Initialize semaphore
    sem_init(&new_message, 0, 1);

    //============================
    // Setup DMA for non-blocking SPI transfers
    dma_channel = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_dreq(&c, spi_get_dreq(SPI_PORT, true));

    dma_channel_configure(dma_channel, &c,
                          &spi_get_hw(SPI_PORT)->dr,  // write to SPI data register
                          &DAC_data_1,                // read from DAC_data_1
                          1,                          // transfer 1 halfword
                          false                       // don't start immediately
    );

    //============================
    // UDP recenve ISR routines
    udpecho_raw_init();

    //============================
    // UDP transmit setup
    udp_tx_pcb = udp_new();
    if (udp_tx_pcb != NULL) {
        ipaddr_aton(BEACON_TARGET, &tx_target_addr);
        printf("UDP transmit initialized for %s:%d\n", BEACON_TARGET, UDP_PORT);
    } else {
        printf("Failed to create UDP TX PCB\n");
    }

    // Enable the interrupt for the alarm (we're using Alarm 0)
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM);
    // Associate an interrupt handler with the ALARM_IRQ
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq);
    // Set to highest priority to ensure it runs with WiFi
    irq_set_priority(ALARM_IRQ, 0x00);  // Highest priority (0 = highest, 255 = lowest)
    // Enable the alarm interrupt
    irq_set_enabled(ALARM_IRQ, true);
    // Initialize the next alarm time and arm the timer
    next_alarm_time = timer_hw->timerawl + DELAY;
    timer_hw->alarm[ALARM_NUM] = next_alarm_time;
    
    printf("Timer alarm initialized and armed\n");

    // Add threads, start scheduler
    pt_add_thread(protothread_send);
    pt_add_thread(protothread_tx_audio);  // Add audio transmit thread
    pt_add_thread(protothread_receive);   // Add bandwidth monitoring thread
    pt_schedule_start;

    return 0;
}