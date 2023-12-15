#ifndef LED_H
#define LED_H

// #define LED_PIN 18

/* Number of individual LEDs */
/* You can have up to 80,000 LEDs before you run out of memory */
// #define LED_NUM 15

/* Leave alone, only defined to hammer it into the compilers head */
#define LED_DATA_SIZE 3
#define LED_BYTE_SIZE LED_NUM * LED_DATA_SIZE

void set_led(uint32_t led, uint8_t r, uint8_t g, uint8_t b);
void set_all(uint8_t r, uint8_t g, uint8_t b);
void send_led_data(ledpins);

#endif
