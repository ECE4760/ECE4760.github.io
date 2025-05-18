Conclusions
==========================================================================

This project built a LED controller system for Cornell's clocktower using 
Raspberry Pi Pico W microcontrollers. Our system successfully implemented 
a modular control architecture based on three types of boards (controller, 
sensor and face boards) and a custom CAN bus communciation, providing a 
user-friendly control panel for Chimesmasters to set and adjust lighting 
behavior of the clocktower.

Our final system achieved all of the core goals we set in the proposal:

* The CAN bus supported data sending and receiving among boards based on Pico 
  W's PIO and DMA
* On the core board, one switch determined LEDs to be turned on or off, one 
  rotary switch determined the LED modes, and three potentiometer sliders 
  controlled the brightness of LED's R/G/B channels using PWM
* The face boards responded accurately to R/G/B color updates when sliding 
  the potentiometers on the core board
* The sensor board monitored and returned sound data to the core board in 
  the ``SOUND`` mode, and then achieving the control of lighting brightness 
  by the external sound level

While the system performed well during demonstration, there were a few
aspects of our project that could use improvement:

* We suffered some hardware issues with the shorting of one of the face
  boards, as well as the unorthodox CAN bus behavior requiring
  jump starting, as discussed in :doc:`hw_des`
* Our current potentiometer sliders grew strikingly warm during operation,
  specifically when the tapped output was at its highest.
  While this could be temporarily fixed by storing a color value when
  setting, then returning the sliders to the down position, a better solution
  would be diagnosing why the behavior happens. We were unable to determine
  the cause (as all measured resistances were normal), indicating that testing
  with other sliders may be useful
* While we could communicate with our ambient light sensor, we consistently
  read values of ``0`` across the I\ :sup:`2`\ C lines, and were therefore
  unable to implement the light-based mode of the system. This isn't a
  regression compared to the current system, but would be nice to figure out
  and implement in the future

Looking forward, our system provides a foundation for future expansion like 
WiFi/Bluetooth enables remoting control interfaces, and also the distributed 
hardware design allows more clock faces or connecting other buildings or devices.

In the end, there were no external standards governing our project. However, 
we reused Prof. Adams' `CAN bus implementation <https://vanhunteradams.com/Pico/CAN/CAN.html>`_,
and our team's PWM implementation used in Lab 3.

The only safety concern was with the high-power draw of our LEDs, and whether
our boards would be able to tolerate the large current draw. After careful
design and testing in lab, our boards were able to withstand this current
draw without noticeable temperature increase, indicating that they would
be well-suited for the tower LEDs.
