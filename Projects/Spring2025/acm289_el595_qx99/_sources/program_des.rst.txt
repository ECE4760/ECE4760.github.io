Program Design
==========================================================================
We designed the system around the core control board, which performed the data processing and control of the face boards. The system is modular and supports any number of face boards, and all communication goes through the core board.

Communication System
--------------------------------------------------------------------------

Our boards communicate to one another using CAN bus messages. The sensor board does not communicate with the face boards, and both communicate through the core. The sensor board reports raw sensor data to the core, which then converts this to a light color to send to the face boards (dependent on the mode of the system).

Every CAN message can be represented as an array of shorts (16 bit values). The first 16 bits represents the arbitration ID, which indicates the board that the message is intended for. Next, the length of the message (in shorts) is encoded in a byte (along with one byte of padding).

Finally, this is followed by the data payload, and terminated with a CRC checksum. The payload starts with a single short representing the message type, then followed by raw data. There are $4$ different message types, which are:

1. ``SENSOR_LIGHT`` represents the transferring of a light sensor *request* to the sensor board. This toggles the sensor board into light sensor mode, which will cause it to repeatedly send light sensor data from the sensor board to the core. Data messages also use this same message type. The data payload is empty for the request, and contains a ``32-bit fix15`` value for the response (packed into two shorts).
2. ``SENSOR_SOUND`` does the same as above, but requesting sound data instead.
3. ``SET_COLOR`` is used to set the color of the face boards. The data payload contains 3 shorts, each containing a byte value from 0-255 representing the red, green, and blue values of the color.
4. ``SET_TOGGLE`` toggles the sensor and face boards on and off. For the face boards, this turns off the lights, and for the sensor board this disables the data messages.

Our CAN bus implementation is a modified version of Professor Adam's `CAN bus implementation <https://github.com/vha3/Hunter-Adams-RP2040-Demos/tree/master/Networking/CAN>`_, made to support sending and receiving in shorts, rather than receiving in bytes and sending in shorts. 

Controller Board
--------------------------------------------------------------------------

The controller board is the core of the system, and controls the rest of the boards. It receives user input via the physical switches on the board, and uses these to control the rest of the system. 

The system has three main modes, selected by a rotary switch:

1. Light mode, where the system is turned on or off based on whether ambient light is detected below 1 lux.
2. Sound mode, where the intensity of the light pulsates with the level of sound detected by the microphone.
3. Manual mode, where the light is always on.

The color of the light is set using the three physical sliders, allowing the user to control the color of the light in RGB. This is the base light color shown in all 3 modes.

The controller board has a main thread managing the system running at a rate of 100Hz. This thread reads sensor data from the sensor board, and sends update messages to the face boards. It also reads values from input switches in order to control the system.


Sensor Board
--------------------------------------------------------------------------

The sensor board is responsible for reading the ambient light and sound levels, and sending this data to the controller board. It has three modes:

1. ``LIGHT`` mode, in which the board sends light sensor data to the core at a rate of 100Hz.
2. ``SOUND`` mode, in which the board sends sound sensor data to the core at a rate of 100Hz.
3. ``DISABLED`` mode, where the system is turned off and does not send any data.

The modes are controlled by the core board via the special ``SENSOR_LIGHT`` and ``SENSOR_SOUND`` messages, as well as the ``SET_TOGGLE`` message to turn the system off.

The light sensor values are read through I2C, and raw values are converted to lux using the resolution of the light sensor via ``lux = resolution * raw value``. This resolution is calcula ted using the formula ``6.72 / (gain * integration_time)``, which was devised from the resolution datatable of the `light sensor datasheet <https://www.vishay.com/docs/84367/designingveml6030.pdf>`_. The system also supports changing gain and integration time, but these are fixed for the project.

For the sound sensor, we decided to read raw ADC values rather than converting to decibels. This is because decibels are in logarithmic scale, which would mean the core would have to convert back to a linear scale anyway, wasting extra compute resources. In order to calculate the amplitude of audio, we sample the audio sensor on its own thread at 10kHz, and calculate the difference between the highest and lowest values in the last 1000 samples. This value represents the height of the dominant audio frequency, and thus is a good estimator of the amplitude of the sound.


Face Board
--------------------------------------------------------------------------

The face board is the simplest board, each responsible for controlling the lights of one of the four clock faces. It does not send any data back to the core, and only receives messages from the core. The face board can either be on or off, and the color of the lights is set by the core board.

Because the LED strips used in the project have RGBW light, the RGB light values are converted to RGBW using the formula ``(R, G, B) => (R-W, G-W, B-W, W=min(R, G, B))``. Additionally, gamma correction is performed via a lookup table, and the system supports white balancing, which can be tuned using a special calibration script.


Software Difficulties
---------------------------------------------------------------------------
The majority of difficulties with the software design was with the CAN modules. A number of issues were encountered with the CAN bus implementation, which took up the majority of our debugging effort.

DMA channels
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
The first issue we encountered was with the DMA channels used by CAN. Because the CAN implementation was written for the Pico, rather than the Pico W, we realized that DMA channels 1 and 2 were already in use (provided ``cyw43_arch_init`` is called). This lead to the system immediately crashing when trying to claim the hardcoded channels. This was resolved by having the CAN implementation use DMA channels 5 and 6 instead.

PIO Deadlock
""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""""
Another issue we encountered was with the PIO implementation of CAN. Although we are not exactly sure why (as described in the hardware design documentation), we suspect there to be a deadlock in the PIO states until every board sends and receives at least one message. To solve this via software, we included dummy messages sent to invalid IDs in the clock face boards, and we also short the CAN bus temporarily to force the PIO to enter the correct state.

