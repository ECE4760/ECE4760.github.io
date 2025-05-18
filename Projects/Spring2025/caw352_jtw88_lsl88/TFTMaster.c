/* Code rewritten from Adafruit Arduino library for the TFT
 *  by Syed Tahmid Mahbub
 * The TFT itself is Adafruit product 1480
 * Included below is the text header from the original Adafruit library
 *  followed by the code
 */

/*
This is the core graphics library for all our displays, providing a common
set of graphics primitives (points, lines, circles, etc.).  It needs to be
paired with a hardware-specific library for each display device we carry
(to handle the lower-level functions).

Adafruit invests time and resources providing this open source code, please
support Adafruit & open-source hardware by purchasing products from Adafruit!

Copyright (c) 2013 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
*/

/**
 * Parth Sarthi Sharma (pss242@cornell.edu)
 * 
 *
 * HARDWARE CONNECTIONS
 *  - GPIO 18 ---> TFT Chip Select
 *  - GPIO 19 ---> TFT MOSI
 *  - GPIO 17 ---> TFT SCK
 *  - GPIO 16 ---> TFT D/C
 *  - GPIO 20 ---> TFT RST
 *
 * RESOURCES USED
 *  - PIO state machines 0 on PIO instance 0
 *
 * NOTE
 *  - This is a translation of the display primitives
 *    for the PIC32 written by Bruce Land and students
 *	  with an added bitmaps function.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"
#include "SPIPIO.pio.h" //Our assembled program
#include "TFTMaster.h" //Header file
#include "glcdfont.c" //Font file

#define pgm_read_byte(addr) (*(const unsigned char *)(addr)) //Read byte at the address

#define NOP asm volatile("nop") //A single no-operation
#define wait8 NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP; //NOPs for drawing pixels
#define wait16 wait8 wait8 //NOPs for drawing pixels

unsigned short _width, _height; //Width and height of the TFT
unsigned short cursor_y, cursor_x, textsize, textcolor, textbgcolor, wrap, rotation;

typedef struct pio_spi_inst{ //PIO SPI struct 
    PIO pio; //The PIO
    uint sm; //State machine
    uint cs_pin; //Chip select
} pio_spi_inst_t;

PIO pio = pio0; //Identifier for the first (PIO 0) hardware PIO instance
uint sm = 0; //The state machine 
	
pio_spi_inst_t spi = { //The SPI instance
	.pio = pio0,
	.sm = 0,
	.cs_pin = CS
};

uint offset; //Offset for the program to load

volatile char flag = 1; //flag to mark completion of an SPI transaction

void pioPinHandler(){ //The PIO interrupt handler
	pio_interrupt_clear(pio0, 0); //Clear a particular PIO interrupt
	flag = 0; //Clear the flag
}

//Function to intialize all the hardware associated with the TFT
void tft_init_hw(void){
	_width = ILI9340_TFTWIDTH;
	_height = ILI9340_TFTHEIGHT;
  
	gpio_init(CS); //Initialize the CS pin as GPIO 
    gpio_set_dir(CS, GPIO_OUT); //Set the pin direction as output
    gpio_put(CS, 1); //Drive CS high
	
	gpio_init(RST); //Initialize the RST pin as GPIO 
	gpio_set_dir(RST, GPIO_OUT); //Set the pin direction as output
	
	gpio_init(DC); //Initialize the DC pin as GPIO
	gpio_set_dir(DC, GPIO_OUT); //Set the pin direction as output
	
	offset = pio_add_program(spi.pio, &spi_cpha0_cs_program); //Attempt to load the program
    pio_spi_cs_init(spi.pio, spi.sm, offset, 8, 1, false, false, SCK, MOSI); //Initialize the SPI program
	pio_interrupt_clear(spi.pio, 0); //Clear a particular PIO interrupt
    pio_set_irq0_source_enabled(spi.pio, PIO_INTR_SM0_LSB, true); //Enable/Disable a single source on a PIO's IRQ 0
	irq_set_exclusive_handler(PIO0_IRQ_0, pioPinHandler); //Set an exclusive interrupt handler for an interrupt on the executing core
	irq_set_enabled(PIO0_IRQ_0, true); //Enable or disable a specific interrupt on the executing core
	sleep_ms(500); //Sleep for 500ms
}

static inline void _rst_low(){ //Function to set the RST pin low
	gpio_put(RST, 0);
}

static inline void _rst_high(){ //Function to set the RST pin high
	gpio_put(RST, 1);
}

static inline void _dc_low(){ //Function to set the RST pin low
	gpio_put(DC, 0);
}

static inline void _dc_high(){ //Function to set the DC pin high
	gpio_put(DC, 1);
}

static inline void _cs_low(){ //Function to set the RST pin low
	gpio_put(CS, 0);
}

static inline void _cs_high(){ //Function to set the CS pin high
	gpio_put(CS, 1);
}

//Function to transmit a word to the SPI PIO
void __time_critical_func(pio_spi_write8_blocking)(const pio_spi_inst_t *spi, const uint8_t *src, size_t len){
    size_t tx_remain = len;
    io_rw_8 *txfifo = (io_rw_8 *) &spi->pio->txf[spi->sm];
    while (tx_remain){
        if (tx_remain && !pio_sm_is_tx_fifo_full(spi->pio, spi->sm)){
            *txfifo = *src++;
            --tx_remain;
        }
    }
}


void tft_spiwrite8(unsigned char c) { //Write 8 bits to the PIO
	pio_spi_write8_blocking(&spi, &c, 1);
}

void tft_spiwrite16(unsigned short c) { //Write 16 bits to the PIO
	uint8_t data = (uint8_t) (c >> 8);
	pio_spi_write8_blocking(&spi, &data, 1); //Send lower 8 bits
	while(flag); //Wait for the transaction to complete
	flag = 1;
	data = (uint8_t) (c & 0xFF);
	pio_spi_write8_blocking(&spi, &data, 1); //Send upper 8 bits
}

void tft_writecommand(unsigned char c) { //Send a command to the TFT screen
    _dc_low();
	_cs_low();
    tft_spiwrite8(c);
	while(flag);
	flag = 1;
	_cs_high();
}

void tft_writedata(unsigned char c) { //Send 8-bit data to the TFT screen
    _dc_high();
	_cs_low();
    tft_spiwrite8(c);
	while(flag);
	flag = 1;
	_cs_high();
}

void tft_writedata16(unsigned short c) { //Send 16-bit data to the TFT screen
    _dc_high();
	_cs_low();
    tft_spiwrite16(c);
	while(flag);
	flag = 1;
	_cs_high();
}

void tft_begin(void) { //Initialize the TFT screen
	sleep_ms(500);
	_rst_low();
	_dc_low();
	_cs_high();

	_rst_high();
	sleep_ms(5);
	_rst_low();
	sleep_ms(20);
	_rst_high();
	sleep_ms(150);

	tft_writecommand(0xEF);
	tft_writedata(0x03);
	tft_writedata(0x80);
	tft_writedata(0x02);

	tft_writecommand(0xCF);
	tft_writedata(0x00);
	tft_writedata(0xC1);
	tft_writedata(0x30);

	tft_writecommand(0xED);
	tft_writedata(0x64);
	tft_writedata(0x03);
	tft_writedata(0x12);
	tft_writedata(0x81);

	tft_writecommand(0xE8);
	tft_writedata(0x85);
	tft_writedata(0x00);
	tft_writedata(0x78);

	tft_writecommand(0xCB);
	tft_writedata(0x39);
	tft_writedata(0x2C);
	tft_writedata(0x00);
	tft_writedata(0x34);
	tft_writedata(0x02);

	tft_writecommand(0xF7);
	tft_writedata(0x20);

	tft_writecommand(0xEA);
	tft_writedata(0x00);
	tft_writedata(0x00);

	tft_writecommand(ILI9340_PWCTR1); //Power control
	tft_writedata(0x23); //VRH[5:0]

	tft_writecommand(ILI9340_PWCTR2); //Power control
	tft_writedata(0x10); //SAP[2:0]; BT[3:0]

	tft_writecommand(ILI9340_VMCTR1); //VCM control
	tft_writedata(0x3e);
	tft_writedata(0x28);

	tft_writecommand(ILI9340_VMCTR2); //VCM control2
	tft_writedata(0x86);

	tft_writecommand(ILI9340_MADCTL); //Memory Access Control
	tft_writedata(ILI9340_MADCTL_MX | ILI9340_MADCTL_BGR);

	tft_writecommand(ILI9340_PIXFMT); 
	tft_writedata(0x55);

	tft_writecommand(ILI9340_FRMCTR1);
	tft_writedata(0x00);
	tft_writedata(0x18);

	tft_writecommand(ILI9340_DFUNCTR); //Display Function Control
	tft_writedata(0x08);
	tft_writedata(0x82);
	tft_writedata(0x27);

	tft_writecommand(0xF2); //3Gamma Function Disable
	tft_writedata(0x00);

	tft_writecommand(ILI9340_GAMMASET); //Gamma curve selected
	tft_writedata(0x01);

	tft_writecommand(ILI9340_GMCTRP1); //Set Gamma
	tft_writedata(0x0F);
	tft_writedata(0x31);
	tft_writedata(0x2B);
	tft_writedata(0x0C);
	tft_writedata(0x0E);
	tft_writedata(0x08);
	tft_writedata(0x4E);
	tft_writedata(0xF1);
	tft_writedata(0x37);
	tft_writedata(0x07);
	tft_writedata(0x10);
	tft_writedata(0x03);
	tft_writedata(0x0E);
	tft_writedata(0x09);
	tft_writedata(0x00);

	tft_writecommand(ILI9340_GMCTRN1); //Set Gamma
	tft_writedata(0x00);
	tft_writedata(0x0E);
	tft_writedata(0x14);
	tft_writedata(0x03);
	tft_writedata(0x11);
	tft_writedata(0x07);
	tft_writedata(0x31);
	tft_writedata(0xC1);
	tft_writedata(0x48);
	tft_writedata(0x08);
	tft_writedata(0x0F);
	tft_writedata(0x0C);
	tft_writedata(0x31);
	tft_writedata(0x36);
	tft_writedata(0x0F);

	tft_writecommand(ILI9340_SLPOUT); //Exit Sleep
	sleep_ms(120);
	tft_writecommand(ILI9340_DISPON); //Display on
	sleep_ms(500);
}

/* Draw a pixel at location (x,y) with given color
 * Parameters:
 *      x:  x-coordinate of pixel to draw; top left of screen is x=0
 *              and x increases to the right
 *      y:  y-coordinate of pixel to draw; top left of screen is y=0
 *              and y increases to the bottom
 *      color:  16-bit color value
 * Returns:     Nothing
 */
void tft_drawPixel(short x, short y, unsigned short color) {
	if((x < 0) ||(x >= _width) || (y < 0) || (y >= _height)){
		return;
	}
	
	_dc_low();
	_cs_low();
    tft_spiwrite8(ILI9340_CASET);
	wait16;
	while(flag);
	flag = 1;
	_cs_high();
	
    _dc_high();
	_cs_low();
    tft_spiwrite16(x);
    wait16;wait16;wait8;
	while(flag);
	flag = 1;
	_cs_high();
	
	_cs_low();
    tft_spiwrite16(x + 1);
    wait16;wait16;wait8;
	while(flag);
	flag = 1;
	_cs_high();
	
    _dc_low();
	_cs_low();
    tft_spiwrite8(ILI9340_PASET);
    wait16;wait8;
	while(flag);
	flag = 1;
	_cs_high();
	
    _dc_high();
	_cs_low();
    tft_spiwrite16(y);
    wait16;wait16;wait8;
	while(flag);
	flag = 1;
	_cs_high();

    _cs_low();
	tft_spiwrite16(y + 1);
    wait16;wait16;wait8;
	while(flag);
	flag = 1;
	_cs_high();
	
    _dc_low();
	_cs_low();
    tft_spiwrite8(ILI9340_RAMWR);
    wait16;wait8;
	while(flag);
	flag = 1;
	_cs_high();
	
    _dc_high();
	_cs_low();
    tft_spiwrite16(color);
    wait16;wait16;wait8;
	while(flag);
	flag = 1;
	_cs_high();
}

void tft_setAddrWindow(unsigned short x0, unsigned short y0, unsigned short x1, unsigned short y1) {
	tft_writecommand(ILI9340_CASET); //Column addr set
	tft_writedata16(x0);
	tft_writedata16(x1);

	tft_writecommand(ILI9340_PASET); //Row addr set
	tft_writedata16(y0);
	tft_writedata16(y1);

	tft_writecommand(ILI9340_RAMWR); //Write to RAM
}

void tft_pushColor(unsigned short color) {
	_dc_high();
	_cs_low();
	tft_spiwrite16(color);
	while(flag);
	flag = 1;
	_cs_high();
}

/* Draw a vertical line at location from (x,y) to (x,y+h-1) with color
 * Parameters:
 *      x:  x-coordinate line to draw; top left of screen is x=0
 *              and x increases to the right
 *      y:  y-coordinate of starting point of line; top left of screen is y=0
 *              and y increases to the bottom
 *      h:  height of line to draw
 *      color:  16-bit color value
 * Returns:     Nothing
 */
void tft_drawFastVLine(short x, short y, short h, unsigned short color) {
	//Rudimentary clipping
	if((x >= _width) || (y >= _height)){
		return;
	}
	if((y + h - 1) >= _height){
		h = _height - y;
	}

	tft_setAddrWindow(x, y, x, y + h - 1);
	_dc_high();
	_cs_low();
	
	while (h--) {
		tft_spiwrite16(color);
		while(flag);
		flag = 1;
	}
	_cs_high();
}

/* Draw a horizontal line at location from (x,y) to (x+w-1,y) with color
 * Parameters:
 *      x:  x-coordinate starting point of line; top left of screen is x=0
 *              and x increases to the right
 *      y:  y-coordinate of starting point of line; top left of screen is y=0
 *              and y increases to the bottom
 *      w:  width of line to draw
 *      color:  16-bit color value
 * Returns:     Nothing
 */
void tft_drawFastHLine(short x, short y, short w, unsigned short color){
	//Rudimentary clipping
	if((x >= _width) || (y >= _height)){
		return;
	}
	if((x + w - 1) >= _width){
		w = _width - x;
	}

	tft_setAddrWindow(x, y, x + w - 1, y);
	_dc_high();
	_cs_low();
	
	while (w--) {
		tft_spiwrite16(color);
		while(flag);
		flag = 1;
	}
	_cs_high();
}

/* Fill entire screen with given color
 * Parameters:
 *      color: 16-bit color value
 * Returs:  Nothing
 */
void tft_fillScreen(unsigned short color) {
    tft_fillRect(0, 0,  _width, _height, color);
}

/* Draw a filled rectangle with starting top-left vertex (x,y),
 *  width w and height h with given color
 * Parameters:
 *      x:  x-coordinate of top-left vertex; top left of screen is x=0
 *              and x increases to the right
 *      y:  y-coordinate of top-left vertex; top left of screen is y=0
 *              and y increases to the bottom
 *      w:  width of rectangle
 *      h:  height of rectangle
 *      color:  16-bit color value
 * Returns:     Nothing
 */
void tft_fillRect(short x, short y, short w, short h, unsigned short color) {
	//Rudimentary clipping
	if((x >= _width) || (y >= _height)){
		return;
	}
	if((x + w - 1) >= _width){
		w = _width  - x;
	}
	if((y + h - 1) >= _height){
		h = _height - y;
	}
	tft_setAddrWindow(x, y, x + w - 1, y + h - 1);
	_dc_high();
	_cs_low();
	for(y = h; y > 0; y--) {
		for(x = w; x > 0; x--) {
			tft_spiwrite16(color);
			while(flag);
			flag = 1;
		}
	}
	// while(flag);
	// flag = 1;
	_cs_high();
}

/* Pass 8-bit (each) R,G,B, get back 16-bit packed color
 * Parameters:
 *      r:  8-bit R/red value from RGB
 *      g:  8-bit g/green value from RGB
 *      b:  8-bit b/blue value from RGB
 * Returns:
 *      16-bit packed color value for color info
 */
inline unsigned short tft_Color565(unsigned char r, unsigned char g, unsigned char b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void tft_setRotation(unsigned char m) {
	unsigned char rotation;
	tft_writecommand(ILI9340_MADCTL);
	rotation = m % 4; //Can't be higher than 3
	switch (rotation) {
		case 0: tft_writedata(ILI9340_MADCTL_MX | ILI9340_MADCTL_BGR);
				_width  = ILI9340_TFTWIDTH;
				_height = ILI9340_TFTHEIGHT;
				break;
		case 1: tft_writedata(ILI9340_MADCTL_MV | ILI9340_MADCTL_BGR);
				_width  = ILI9340_TFTHEIGHT;
				_height = ILI9340_TFTWIDTH;
				break;
		case 2: tft_writedata(ILI9340_MADCTL_MY | ILI9340_MADCTL_BGR);
				_width  = ILI9340_TFTWIDTH;
				_height = ILI9340_TFTHEIGHT;
				break;
		case 3: tft_writedata(ILI9340_MADCTL_MV | ILI9340_MADCTL_MY | ILI9340_MADCTL_MX | ILI9340_MADCTL_BGR);
				_width  = ILI9340_TFTHEIGHT;
				_height = ILI9340_TFTWIDTH;
				break;
	}
}

/* Draw a circle outline with center (x0,y0) and radius r, with given color
 * Parameters:
 *      x0: x-coordinate of center of circle. The top-left of the screen
 *          has x-coordinate 0 and increases to the right
 *      y0: y-coordinate of center of circle. The top-left of the screen
 *          has y-coordinate 0 and increases to the bottom
 *      r:  radius of circle
 *      color: 16-bit color value for the circle. Note that the circle
 *          isn't filled. So, this is the color of the outline of the circle
 * Returns: Nothing
 */
void tft_drawCircle(short x0, short y0, short r, unsigned short color) {
	short f = 1 - r;
	short ddF_x = 1;
	short ddF_y = -2 * r;
	short x = 0;
	short y = r;

	tft_drawPixel(x0, y0 + r, color);
	tft_drawPixel(x0, y0 - r, color);
	tft_drawPixel(x0 + r, y0, color);
	tft_drawPixel(x0 - r, y0, color);

	while(x < y) {
		if(f >= 0){
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;

		tft_drawPixel(x0 + x, y0 + y, color);
		tft_drawPixel(x0 - x, y0 + y, color);
		tft_drawPixel(x0 + x, y0 - y, color);
		tft_drawPixel(x0 - x, y0 - y, color);
		tft_drawPixel(x0 + y, y0 + x, color);
		tft_drawPixel(x0 - y, y0 + x, color);
		tft_drawPixel(x0 + y, y0 - x, color);
		tft_drawPixel(x0 - y, y0 - x, color);
	}
}

//Helper function for drawing circles and circular objects
void tft_drawCircleHelper( short x0, short y0, short r, unsigned char cornername, unsigned short color){
	short f = 1 - r;
	short ddF_x = 1;
	short ddF_y = -2 * r;
	short x = 0;
	short y = r;

	while(x < y){
		if(f >= 0){
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;
		if(cornername & 0x4){
			tft_drawPixel(x0 + x, y0 + y, color);
			tft_drawPixel(x0 + y, y0 + x, color);
		}
		if (cornername & 0x2) {
			tft_drawPixel(x0 + x, y0 - y, color);
			tft_drawPixel(x0 + y, y0 - x, color);
		}
		if (cornername & 0x8) {
			tft_drawPixel(x0 - y, y0 + x, color);
			tft_drawPixel(x0 - x, y0 + y, color);
		}
		if (cornername & 0x1) {
			tft_drawPixel(x0 - y, y0 - x, color);
			tft_drawPixel(x0 - x, y0 - y, color);
		}
	}
}

/* Draw a filled circle with center (x0,y0) and radius r, with given color
 * Parameters:
 *      x0: x-coordinate of center of circle. The top-left of the screen
 *          has x-coordinate 0 and increases to the right
 *      y0: y-coordinate of center of circle. The top-left of the screen
 *          has y-coordinate 0 and increases to the bottom
 *      r:  radius of circle
 *      color: 16-bit color value for the circle
 * Returns: Nothing
 */
void tft_fillCircle(short x0, short y0, short r, unsigned short color) {
	tft_drawFastVLine(x0, y0 - r, 2 * r + 1, color);
	tft_fillCircleHelper(x0, y0, r, 3, 0, color);
}

//Helper function for drawing filled circles
void tft_fillCircleHelper(short x0, short y0, short r, unsigned char cornername, short delta, unsigned short color) {
	short f = 1 - r;
	short ddF_x = 1;
	short ddF_y = -2 * r;
	short x = 0;
	short y = r;

	while(x < y){
		if (f >= 0) {
			y--;
			ddF_y += 2;
			f += ddF_y;
		}
		x++;
		ddF_x += 2;
		f += ddF_x;

		if (cornername & 0x1) {
			tft_drawFastVLine(x0 + x, y0 - y, 2 * y + 1 + delta, color);
			tft_drawFastVLine(x0 + y, y0 - x, 2 * x + 1 + delta, color);
		}
		if (cornername & 0x2) {
			tft_drawFastVLine(x0 - x, y0 - y, 2 * y + 1 + delta, color);
			tft_drawFastVLine(x0 - y, y0 - x, 2 * x + 1 + delta, color);
		}
	}
}

//Bresenham's algorithm

/* Draw a straight line from (x0,y0) to (x1,y1) with given color
 * Parameters:
 *      x0: x-coordinate of starting point of line. The x-coordinate of
 *          the top-left of the screen is 0. It increases to the right.
 *      y0: y-coordinate of starting point of line. The y-coordinate of
 *          the top-left of the screen is 0. It increases to the bottom.
 *      x1: x-coordinate of ending point of line. The x-coordinate of
 *          the top-left of the screen is 0. It increases to the right.
 *      y1: y-coordinate of ending point of line. The y-coordinate of
 *          the top-left of the screen is 0. It increases to the bottom.
 *      color: 16-bit color value for line
 */
void tft_drawLine(short x0, short y0, short x1, short y1, unsigned short color) {
	short steep = abs(y1 - y0) > abs(x1 - x0);
	if(steep){
		swap(x0, y0);
		swap(x1, y1);
	}

	if (x0 > x1) {
		swap(x0, x1);
		swap(y0, y1);
	}

	short dx, dy;
	dx = x1 - x0;
	dy = abs(y1 - y0);

	short err = dx / 2;
	short ystep;

	if (y0 < y1) {
		ystep = 1;
	}
	else {
		ystep = -1;
	}

	for(; x0 <= x1; x0++){
		if (steep) {
			tft_drawPixel(y0, x0, color);
		}
		else {
			tft_drawPixel(x0, y0, color);
		}
		err -= dy;
		if (err < 0) {
			y0 += ystep;
			err += dx;
		}
	}
}

/* Draw a rectangle outline with top left vertex (x,y), width w
 * and height h at given color
 * Parameters:
 *      x:  x-coordinate of top-left vertex. The x-coordinate of
 *          the top-left of the screen is 0. It increases to the right.
 *      y:  y-coordinate of top-left vertex. The y-coordinate of
 *          the top-left of the screen is 0. It increases to the bottom.
 *      w:  width of the rectangle
 *      h:  height of the rectangle
 *      color:  16-bit color of the rectangle outline
 * Returns: Nothing
 */
void tft_drawRect(short x, short y, short w, short h, unsigned short color) {
	tft_drawFastHLine(x, y, w, color);
	tft_drawFastHLine(x, y + h - 1, w, color);
	tft_drawFastVLine(x, y, h, color);
	tft_drawFastVLine(x + w - 1, y, h, color);
}

/* Draw a rounded rectangle outline with top left vertex (x,y), width w,
 * height h and radius of curvature r at given color
 * Parameters:
 *      x:  x-coordinate of top-left vertex. The x-coordinate of
 *          the top-left of the screen is 0. It increases to the right.
 *      y:  y-coordinate of top-left vertex. The y-coordinate of
 *          the top-left of the screen is 0. It increases to the bottom.
 *      w:  width of the rectangle
 *      h:  height of the rectangle
 *      color:  16-bit color of the rectangle outline
 * Returns: Nothing
 */
void tft_drawRoundRect(short x, short y, short w, short h, short r, unsigned short color) {
	tft_drawFastHLine(x + r, y, w - 2 * r, color);
	tft_drawFastHLine(x + r, y + h - 1, w-2*r, color);
	tft_drawFastVLine(x, y + r, h - 2 * r, color);
	tft_drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
	
	tft_drawCircleHelper(x + r, y + r, r, 1, color);
	tft_drawCircleHelper(x + w - r - 1, y + r, r, 2, color);
	tft_drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color);
	tft_drawCircleHelper(x + r, y + h - r - 1, r, 8, color);
}

//Fill a rounded rectangle
void tft_fillRoundRect(short x, short y, short w, short h, short r, unsigned short color) {
	tft_fillRect(x + r, y, w - 2 * r, h, color);

	tft_fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
	tft_fillCircleHelper(x + r, y + r, r, 2, h - 2 * r - 1, color);
}

/* Draw a triangle outline with vertices (x0,y0),(x1,y1),(x2,y2) with given color
 * Parameters:
 *      x0: x-coordinate of one of the 3 vertices
 *      y0: y-coordinate of one of the 3 vertices
 *      x1: x-coordinate of one of the 3 vertices
 *      y1: y-coordinate of one of the 3 vertices
 *      x2: x-coordinate of one of the 3 vertices
 *      y2: y-coordinate of one of the 3 vertices
 *      color: 16-bit color value for outline
 * Returns: Nothing
 */
void tft_drawTriangle(short x0, short y0, short x1, short y1, short x2, short y2, unsigned short color) {
	tft_drawLine(x0, y0, x1, y1, color);
	tft_drawLine(x1, y1, x2, y2, color);
	tft_drawLine(x2, y2, x0, y0, color);
}

/* Draw a filled triangle with vertices (x0,y0),(x1,y1),(x2,y2) with given color
 * Parameters:
 *      x0: x-coordinate of one of the 3 vertices
 *      y0: y-coordinate of one of the 3 vertices
 *      x1: x-coordinate of one of the 3 vertices
 *      y1: y-coordinate of one of the 3 vertices
 *      x2: x-coordinate of one of the 3 vertices
 *      y2: y-coordinate of one of the 3 vertices
 *      color: 16-bit color value
 * Returns: Nothing
 */
void tft_fillTriangle (short x0, short y0, short x1, short y1, short x2, short y2, unsigned short color) {
	short a, b, y, last;

	if(y0 > y1){
		swap(y0, y1);
		swap(x0, x1);
	}
	if(y1 > y2){
		swap(y2, y1);
		swap(x2, x1);
	}
	if(y0 > y1){
		swap(y0, y1);
		swap(x0, x1);
	}

	if(y0 == y2){
		a = b = x0;
		if(x1 < a){
			a = x1;
		}
		else if(x1 > b){
			b = x1;
		}
		if(x2 < a){
			a = x2;
		}
		else if(x2 > b){
			b = x2;
		}
		tft_drawFastHLine(a, y0, b - a + 1, color);
		return;
	}

	short
	dx01 = x1 - x0,
	dy01 = y1 - y0,
	dx02 = x2 - x0,
	dy02 = y2 - y0,
	dx12 = x2 - x1,
	dy12 = y2 - y1,
	sa = 0,
	sb = 0;
	
	// For upper part of triangle, find scanline crossings for segments
	// 0-1 and 0-2.  If y1=y2 (flat-bottomed triangle), the scanline y1
	// is included here (and second loop will be skipped, avoiding a /0
	// error there), otherwise scanline y1 is skipped here and handled
	// in the second loop...which also avoids a /0 error here if y0=y1
	// (flat-topped triangle).
	
	if(y1 == y2){
		last = y1;
	}
	else{
		last = y1 - 1;
	}

	for(y=y0; y<=last; y++) {
		a   = x0 + sa / dy01;
		b   = x0 + sb / dy02;
		sa += dx01;
		sb += dx02;
		/* longhand:
		a = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
		b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
		*/
		if(a > b){
			swap(a,b);
		}
		tft_drawFastHLine(a, y, b - a + 1, color);
	}
	// For lower part of triangle, find scanline crossings for segments
	// 0-2 and 1-2.  This loop is skipped if y1 = y2.
	sa = dx12 * (y - y1);
	sb = dx02 * (y - y0);
	for(; y<=y2; y++) {
		a   = x1 + sa / dy12;
		b   = x0 + sb / dy02;
		sa += dx12;
		sb += dx02;
		/* longhand:
		a = x1 + (x2 - x1) * (y - y1) / (y2 - y1);
		b = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
		*/
		if(a > b){
			swap(a, b);
		}
		tft_drawFastHLine(a, y, b - a + 1, color);
	}
}

//Function to draw the bitmaps on the TFT
void tft_drawBitmap(short x, short y, const unsigned char *bitmap, short w, short h, unsigned short color){
	//Rudimentary clipping
	if((x >= _width) || (y >= _height)) return;
    if((x + w - 1) >= _width)  w = _width  - x;
    if((y + h - 1) >= _height) h = _height - y;

	short i, j, byteWidth = (w + 7) / 8;
    tft_setAddrWindow(x, y, x + w - 1, y + h - 1);
    _dc_high();
    _cs_low();
    for(j = 0; j < h; j++){
        for(i = 0; i < w; i++){
            if(pgm_read_byte(bitmap + j * byteWidth + i / 8) & (128 >> (i & 7))) {
                tft_spiwrite16(color);
				while(flag);
				flag = 1;
            }
            else{
                tft_spiwrite16(0x0000);
				while(flag);
				flag = 1;
            }
        }
    }
	_cs_high();
}

void tft_write(unsigned char c){
	if (c == '\n') {
		cursor_y += textsize*8;
		cursor_x  = 0;
	}
	else if (c == '\r') {
		
	}
	else if (c == '\t'){
		int new_x = cursor_x + tabspace;
		if (new_x < _width){
			cursor_x = new_x;
		}
	}
	else {
		tft_drawChar(cursor_x, cursor_y, c, textcolor, textbgcolor, textsize);
		cursor_x += textsize * 6;
		if (wrap && (cursor_x > (_width - textsize * 6))) {
			cursor_y += textsize * 8;
			cursor_x = 0;
		}
	}
}

/* Print text onto screen
 * Call tft_setCursor(), tft_setTextColor(), tft_setTextSize()
 *  as necessary before printing
 */
inline void tft_writeString(char* str){
	while (*str){
		tft_write(*str++);
	}
}

//Draw a character
void tft_drawChar(short x, short y, unsigned char c, unsigned short color, unsigned short bg, unsigned char size) {
	char i, j;
	if((x >= _width) || (y >= _height) || ((x + 6 * size - 1) < 0) || ((y + 8 * size - 1) < 0)){
		return;
	}

	for(i = 0; i < 6; i++){
		unsigned char line;
		if(i == 5){
			line = 0x0;
		}
		else{
			line = pgm_read_byte(font + (c * 5) + i);
		}
		for(j = 0; j < 8; j++){
			if (line & 0x1) {
				if(size == 1){
					tft_drawPixel(x + i, y + j, color);
				}
				else{
					tft_fillRect(x + (i * size), y + (j * size), size, size, color);
				}
			}
			else if (bg != color) {
				if (size == 1){
					tft_drawPixel(x + i, y + j, bg);
				}
				else {
					tft_fillRect(x + i * size, y + j * size, size, size, bg);
				}
			}
			line >>= 1;
		}
	}
}

/*Set size of text to be displayed
 * Parameters:
 *      s = text size (1 being smallest)
 * Returns: nothing
 */
inline void tft_setCursor(short x, short y){
	cursor_x = x;
	cursor_y = y;
}

inline void tft_setTextSize(unsigned char s) {
	textsize = (s > 0) ? s : 1;
}

inline void tft_setTextColor(unsigned short c) {
	textcolor = textbgcolor = c;
}

/* Set color of text to be displayed
 * Parameters:
 *      c = 16-bit color of text
 *      b = 16-bit color of text background
 */
inline void tft_setTextColor2(unsigned short c, unsigned short b) {
	textcolor   = c;
	textbgcolor = b;
}

inline void tft_setTextWrap(char w) {
	wrap = w;
}

/* Returns current roation of screen
 *          0 = no rotation (0 degree rotation)
 *          1 = rotate 90 degree clockwise
 *          2 = rotate 180 degree
 *          3 = rotate 90 degree anticlockwise
 */
inline unsigned char tft_getRotation(void) {
	return rotation;
}

/* Set display rotation in 90 degree steps
 * Parameters:
 *      x: dictate direction of rotation
 *          0 = no rotation (0 degree rotation)
 *          1 = rotate 90 degree clockwise
 *          2 = rotate 180 degree
 *          3 = rotate 90 degree anticlockwise
 * Returns: Nothing
 */
void tft_gfx_setRotation(unsigned char x) {
	rotation = (x & 3);
	switch(rotation) {
		case 0:
		case 2: _width  = ILI9340_TFTWIDTH;
				_height = ILI9340_TFTHEIGHT;
				break;
		case 1:
		case 3: _width  = ILI9340_TFTHEIGHT;;
				_height = ILI9340_TFTWIDTH;
				break;
	}
}

//Return the size of the display (per current rotation)
inline short tft_width(void) {
	return _width;
}

inline short tft_height(void) {
	return _height;
}