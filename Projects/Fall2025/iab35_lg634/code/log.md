# 14/11 
- port https://github.com/raspberrypi/pico-examples/blob/master/usb/device/dev_hid_composite/main.c to a single directory
  - doesn't seem like button is working
- determined which hardware tinyusb uses:
  - ``hw/bsp/rp2040/family.c``:
    - UART?
    - bootsel (retarget)
    - board_init sets sys clock to something 

# 20/11
- attempted to thread USB
- weird irritating synchronisation stuff going on: seems like the input handler cannot be gated behind a semaphore?
- finally (mostly) solved issues

## usb
- gating button behind (== button) doesn't seem to work -- presumably because nulls also need to be sent?
  -> yes, empty key reports are necessary for keyboard. sequencing: queue task for text -> task, complete transfer -> queue empty key -> end

this is easiest handled in a callback.

# break work
work on the pmw3389 driver
the documentation is poor. tried to work through some reference code examples

## sensor
https://github.com/teamspatzenhirn/pmw3389_zephyr_driver/tree/zephyr
https://github.com/qmk/qmk_firmware/blob/master/drivers/sensors/pmw33xx_common.h
https://github.com/dkao/Kensington_Expert_Mouse_PMW3389_Arduino/blob/master/PMW3389DM-Mouse/PMW3389DM-Mouse.ino
https://github.com/Ghost-Girls/PMW3360-3389-PAW3395_STM32-CH32-APM32/blob/main/Code/Src/main.c
https://esp32.com/viewtopic.php?t=38430

are not using the 'auto CS' feature of the SPI peripheral since the SPI implementation of the sensor is somewhat tempermental


### notes on some implementations surveyed

https://github.com/inorichi/zmk-pmw3610-driver/blob/main/src/pmw3610.c
- init
  - (10ms) power -(200ms)-> clear -(50ms)-> check -(0ms)-> configure
- power:
  - reset spi: toggle CS low->high
  - write powerup reset with powerup command reset
- clear:
  - write 0 to REG_OBSERVATION
- check:
  - read REG_OBSERVATION, expect 0x0f in lower bits
  - check product ID
- configure:
  - clear motion registers by reading
  - set various configuration options (this code is zealous about checking it)
- CS function (all CS depends on this)
  - if enable is false, wait T_NCS_SCLK before setting pin (1us)
  - if enable is true, wait T_NCS_SCLK after setting pin
- reg_write
  - CS high
  - write, delay of T_SCLK_NCS_WR (10us)
  - CS low
  - delay of T_SWX (30us)
- reg_read
  - CS
  - write address
  - T_SRAD (4us)
  - read
  - CS low
  - delay of T_SRX (1us)
- burst read
  - CS function
  - write address
  - wait T_SRAD_MOTBR (4us)
  - read back
  - CS low
  - T_BEXIT (1us)
- rate apparently set through rest registers?

has a firmware upload!

https://github.com/epem/Gaming-Mouse/blob/main/3360_Mouse_pico.ino
- setup
  - CS: 1 -(1us)->0 -(1us)->1
  - wait 1us, then boot, then delay 10 (ms)
  - then config, setup buttons
- boot
  - write 0x5a to power_up_reset using spi_write
  - delay 50ms
  - read the registers, upload rom
- spi_write
  - NCS low, delay 1us
  - transfer
  - delay 35us
  - spi end
  - delay 180us
- spi_read
  - NCS low, delay 1us
  - transfer addr
  - delay 160us
  - get return
  - delay 1us
  - write pin high
  - delay 20us
- burst
  - NCS low, delay 1us
  - transfer address
  - 35us
  - get data
  - NCS high, delay 1us
- burst read seems to take enough time that the scroll wheel must be updated as well

has a firmware upload!

## button driver
- array of button_t to initialise
- all gpio interrupts are ORed together, this means that there must be some 'vectoring' depending on gpio number
  - sdk kind of devectors?

- to debounce, record start time, end time, compare against a decreasing set of times
- to vectorise, just use the 32-long array. there are not that many pins to decode anyway

- when button reaches a threshold, it will queue an event with a code based on button number, and button event type
- it is then the queue's resopnsibility to look up what that corresponds to

# todo
- add more event types to, at minimum, support the sensors included
- introduce more advanced event handling system: event chaining, separate hardware / software configuration

clear IRQs at startup
-> this is already done when adding gpio irqs
clear up enum names

- resolve slight burst read complexity: the data should just be written directly somewhere static to avoid alloc

- test encoder
- test if multi-key sends are handled properly


init-time checking of button conflicts
use hardware interpolator for data packing

- DMA for SPI?
leave struct packing / funny things to later
leave separating structs / refactoring to later too


## scheduling
- after a period of activity, run the 'wait until', and then go to sleep
- the event handlers should try to consolidate posted events before passing it off


(usb) to reduce polling, most likely want to have a separate handler for data / transfer events versus mount / unmounts

we want to avoid cases where an input comes in while the hid driver is running, and is thus dropped

atomic counter in the event handler, bitflags or something for each event?

- q: do the handlers queued for a given event need to have the ability to accept args / a unique identifier?
  - this would really only come up for keystroke events
  - so presumably yes. every event handled will be passed some uint32_t, up to function to handle it

- should ratelimit usb


## events
- for now, provide some basic events
(these are what buttons, readings, etc. can trigger)

button uint32 data passing considerations:
- gamepad hid events are a single set bit in 32.
- point is, with clever encoding, uint32_t *should be* enough to support a decent amount of binding

- possible bind_type:
  - mouse button, keyboard
  - support later: gamepad, additional usage pages for e.g. volume controls
- scroll event should have adapters for different inputs

usage pages define how the data should be interpreted

## sensor
- testing
- configuration (at compile time, live)
- async architecture for startup and potentially some of the read / write sequences

## configuration
- webusb (seems to abstract over vendor classes)
- a vendor descriptor / class
- HID firmware control
- DFU

openrazer: https://github.com/openrazer/openrazer/blob/master/driver/razercommon.c#L20

reports seem to be 0x04, 0x05

## rotary encoder
https://github.com/GitJer/Rotary_encoder/
-> port to C. apparently 12 pulses per rotation? otherwise, wondering about chattering
https://github.com/DavidAntliff/esp32-rotary-encoder/tree/master

## debouncing
https://github.com/TuriSc/RP2040-Button
https://nashimus.com/posts/debounce-ky-040/
https://my.eng.utah.edu/~cs5780/debouncing.pdf
