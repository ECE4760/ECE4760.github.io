/* Libraries */
#include "pico/stdlib.h"
#include "LED.h"
/* For WS2812B */
/* Datasheet used: https://cdn-shop.adafruit.com/datasheets/WS2812B.pdf */

/* WARNING: Due to WS2812B being 5V logic and the RP2040 being 3v3 a level shifter is needed */
/* You will not fry the pins or anything if you accidentally hook it up, it just won't register */
/* This is because WS2812B registers 0.7VDD as a minimum for logic high, 0.7*5V => 3.5V which is above what the RP2040 can do */

/* Assembler functions */
/* If you place this in a .cpp file make sure to change /extern/ to /extern "C"/ */
/* eg; extern "C" void function(); */
extern void cycle_delay_t0h();
extern void cycle_delay_t0l();
extern void cycle_delay_t1h();
extern void cycle_delay_t1l();
extern uint32_t disable_and_save_interrupts();			/* Used for interrupt disabling */
extern void enable_and_restore_interrupts(uint32_t);	/* Used for interrupt enabling */

/* The GPIO pin for the LED data */
// #define LED_PIN 18

/* Number of individual LEDs */
/* You can have up to 80,000 LEDs before you run out of memory */
#define LED_NUM 110

/* Leave alone, only defined to hammer it into the compilers head */
#define LED_DATA_SIZE 3
#define LED_BYTE_SIZE LED_NUM * LED_DATA_SIZE

/* We are not here to waste any memory, 3 bytes per LED */
uint8_t led_data[LED_BYTE_SIZE];

/* Sets a specific LED to a certain color */
/* LEDs start at 0 */
void set_led(uint32_t led, uint8_t r, uint8_t g, uint8_t b){
	led_data[led * LED_DATA_SIZE] = g;		/* Green */
	led_data[(led * LED_DATA_SIZE) + 1] = r;	/* Red */
	led_data[(led * LED_DATA_SIZE) + 2] = b;	/* Blue */
}

/* Sets all the LEDs to a certain color */
void set_all(uint8_t r, uint8_t g, uint8_t b){
	for (uint32_t i = 0; i < LED_BYTE_SIZE; i += LED_DATA_SIZE)
	{
		led_data[i] = g;		/* Green */
		led_data[i + 1] = r;		/* Red */
		led_data[i + 2] = b;		/* Blue */
	}
}

/* Sends the data to the LEDs */
void send_led_data(ledpin){	
	/* Disable all interrupts and save the mask */
	uint32_t interrupt_mask = disable_and_save_interrupts();
	
	/* Get the pin bit */
	uint32_t pin = 1UL << ledpin;
	
	/* Declared outside to force optimization if compiler gets any funny ideas */
	uint8_t red = 0;
	uint8_t green = 0;
	uint8_t blue = 0;
	uint32_t i = 0;
	int8_t j = 0;
	
	for (i = 0; i < LED_BYTE_SIZE; i += LED_DATA_SIZE)
	{
		/* Send order is green, red, blue because someone messed up big time */
		    
		/* Look up values once, a micro optimization, assume compiler is dumb as a brick */
		green = led_data[i];
		red = led_data[i + 1];
		blue = led_data[i + 2];
		    
		for (j = 7; j >= 0; j--) /* Handle the 8 green bits */
		{
			/* Get Nth bit */
			if (((green >> j) & 1) == 1) /* The bit is 1 */
			{
				sio_hw->gpio_set = pin; /* This sets the specific pin to high */
				cycle_delay_t1h();		/* Delay by datasheet amount (800ns give or take) */
				sio_hw->gpio_clr = pin; /* This sets the specific pin to low */
				cycle_delay_t1l();		/* Delay by datasheet amount (450ns give or take) */
			}
			else /* The bit is 0 */
			{
				sio_hw->gpio_set = pin;
				cycle_delay_t0h();
				sio_hw->gpio_clr = pin;
				cycle_delay_t0l();
			}
		}
		    
		for (j = 7; j >= 0; j--) /* Handle the 8 red bits */
		{
			if (((red >> j) & 1) == 1)
			{
				sio_hw->gpio_set = pin;
				cycle_delay_t1h();
				sio_hw->gpio_clr = pin;
				cycle_delay_t1l();
			}
			else
			{
				sio_hw->gpio_set = pin;
				cycle_delay_t0h();
				sio_hw->gpio_clr = pin;
				cycle_delay_t0l();
			}
		}
		    
		for (j = 7; j >= 0; j--) /* Handle the 8 blue bits */
		{
			if (((blue >> j) & 1) == 1)
			{
				sio_hw->gpio_set = pin;
				cycle_delay_t1h();
				sio_hw->gpio_clr = pin;
				cycle_delay_t1l();
			}
			else
			{
				sio_hw->gpio_set = pin;
				cycle_delay_t0h();
				sio_hw->gpio_clr = pin;
				cycle_delay_t0l();
			}
		}
	}
	    
	/* Set the level low to indicate a reset is happening */
	sio_hw->gpio_clr = pin;
	
	/* Enable the interrupts that got disabled */
	enable_and_restore_interrupts(interrupt_mask);
	
	/* Make sure to wait any amount of time after you call this function */
}

// int main()
// {
// 	/* System init */
// 	gpio_init(LED_PIN);
// 	gpio_set_dir(LED_PIN, GPIO_OUT);
// 	gpio_put(LED_PIN, false); /* Important to start low to tell the LEDs that it's time for new data */
	
// 	/* 100MHz is a clean number and used to calculate the cycle delays */
// 	set_sys_clock_khz(100000, true);
	
// 	/* Wait a bit to ensure clock is running and force LEDs to reset*/
// 	sleep_ms(10);
	
// 	/* Used for example */
// 	int32_t led = 0;
// 	uint8_t led_dir = 1;
// 	uint8_t dim_value = 1;
// 	uint8_t dim_dir = 1;
	
// 	uint32_t timer = 2; /* Change LEDs every 2ms, basically a speed control, higher is slower */
// 	uint32_t timer_val = 0; /* Track current time */
//     	while (true) 
//    	{
// 	    /* Go crazy */
	    
// 	    /* Only need to update LEDs once every 2ms */
// 	    if (timer_val > timer)
// 	    {
// 		    /*-- I'm using this to dim the LEDs on and off with the color white --*/
	    
// 		    if (dim_value >= 26) /* Start dimming down */
// 		    {
// 			    dim_value = 25;
// 			    dim_dir = 0;
// 		    }
// 		    else if(dim_value == 0) /* Start dimming up */
// 		    {
// 			    dim_dir = 1;
// 		    }
	    
// 		    if (dim_dir)
// 			    dim_value++;
// 		    else
// 			    dim_value--;
	    
// 		    /* Set LED data to dimmed white */
// 		    set_all(dim_value, dim_value, dim_value);
	    
// 		    /*---------------------------------------------------------------------*/
	    
// 		    /*-- I'm using this to race a red LED back and forth across the strip --*/
	    
// 		    if (led < 0) /* Reached end, go back */
// 		    {
// 			    led = 0;
// 			    led_dir = 1;
// 		    }
// 		    else if(led >= LED_NUM - 1) /* Reached other end, go back */
// 		    {
// 			    led = LED_NUM - 1;
// 			    led_dir = 0;
// 		    }
	    
// 		    /* Set new position */
// 		    set_led(led, 100, 0, 0); /* Red */
	    
// 		    /* Move LED for next iteration */
// 		    if (led_dir)
// 			    led++;
// 		    else
// 			    led--;
	    
// 		    /*-----------------------------------------------------------------------*/
		    
// 		    timer_val = 0; /* Reset update cycle */
// 	    }
	    
	    
// 	    /* Send out the color data to the LEDs */
// 	    send_led_data();
	    
// 	    /* Refresh rate for LEDs*/
// 	    /* It is hard to estimate due to the logic above consuming time, but forcing it at 1ms + roughly 2.5ms from LED data transfer + 0.5ms logic above => 4ms per loop */
// 	    /* Which is roughly 250Hz, but again this is a hard guess, it's probably even less */
// 	    /* Also note that this is different from the update rate which is how fast you are updating your LED colors in code */
// 	    sleep_ms(1); 
// 	    timer_val++;
	    
// 	    /* A wait is important like the sleep_ms() above. This is to give the LEDs a notice of reset. It expects anything more than 50us */
//     	}
	
// 	return 0;
// }
