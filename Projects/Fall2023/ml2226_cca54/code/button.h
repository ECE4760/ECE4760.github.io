// Define GPIO pins for buttons
#define RED_BUTTON_PIN 21
#define GREEN_BUTTON_PIN 27
#define YELLOW_BUTTON_PIN 26
#define BLUE_BUTTON_PIN 22

#define DAC_config_chan_B 0b1011000000000000
#define SPI_PORT spi0





// Function declarations
void init_buttons();
int check_buttons();
void redSound();
void blueSound();
void greenSound();
void yellowSound();

