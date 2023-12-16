// led.c
#include "led.h"
#include "pico/stdlib.h"
#include <stdio.h>

void init_leds()
{
    gpio_init(RED_LED_PIN);
    gpio_set_dir(RED_LED_PIN, GPIO_OUT);

    gpio_init(GREEN_LED_PIN);
    gpio_set_dir(GREEN_LED_PIN, GPIO_OUT);

    gpio_init(YELLOW_LED_PIN);
    gpio_set_dir(YELLOW_LED_PIN, GPIO_OUT);

    gpio_init(BLUE_LED_PIN);
    gpio_set_dir(BLUE_LED_PIN, GPIO_OUT);
}

;
void light_led(int pin)
{
    gpio_put(pin, 1);
    sleep_ms(1000);
    gpio_put(pin, 0);
    sleep_ms(1000);
}