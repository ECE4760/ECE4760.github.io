High-Level Design
==========================================================================

Our design is meant to control the lighting of Cornell's McGraw Tower.
While the tower's clockface LEDs can have a large impact on campus, a
robust system is needed to control their color and ensure reliable
operation.

An existing system controls these LEDs through a remote; however,
extended use has found the remote to often be unreliable and hard to
use, as well as lacking functionality beyond basic color changing. Our
system aims to provide reliable communication through a CAN bus (known
for its robust communication from its extensive use in the automotive
industry), as well as incorporate additional sensors for added
functionality, such as pulsing with the bell's sound or turning on and
off with sunrise/sunset. Accordingly, our system is composed of six boards;
the main control board in the tower will receive data from the sensor board
(in the belfry), and will communicate the desired color to the face boards
(one in each clocktower face), which will drive the LEDs appropriately.

.. image:: img/high_level/tower.png
   :align: center
   :width: 70%
   :class: bottompadding

.. image:: img/high_level/high_level.png
   :align: center
   :width: 70%
   :class: bottompadding

While this approach introduces a lot of hardware, it allows for clean
separation of concerns between the boards (i.e. the sensor board can
focus on measurements, and not worry about LED driving). It also allows
for modularity; our system could easily incorporate another face board
without any large changes to the code, for example, as all of the face boards
receive identical information. This also provides a robust distributed system
on which future software could be written (for example, to do more advanced
sentiment analysis of songs and color the faces appropriately).

Beyond the desired functionality, the only requirement for our system is
that it is able to handle the high power of the LED strips, discussed in
the hardware design.