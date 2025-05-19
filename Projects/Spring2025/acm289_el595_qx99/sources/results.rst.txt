Results
==========================================================================

Our system was able to accurately and intuitively control the colors of the
LEDs, as shown below in our demo with a mock-up of the clocktower LEDs.
By making sure our polling rate of the potentiometers and microphone
(100 times a second) was quick enough, our LEDs were able to respond to
any changes of color and sound (in sound mode) without noticeable user
delay. We also had to take the logarithm of our potentiometer output to
account for the approximately exponential taper (shown in
`the datasheet <https://www.bourns.com/docs/Product-Datasheets/pta.pdf>`_
as an "audio taper"); while not perfectly linear, this yielded a sweep of
each color that was linear from a user perspective, allowing them to
intuitively and accurately control the final color.

.. youtube:: BlySUTFZxhw
  :align: center
  :width: 70%