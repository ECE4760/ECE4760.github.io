.. ECE 5730: Clocktower LEDs documentation master file, created by
   sphinx-quickstart on Mon Apr 28 14:17:50 2025.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

ECE 5730: Clocktower LEDs
==========================================================================

.. toctree::
   :maxdepth: 2
   :caption: Documentation Sections
   :hidden:

   high_level
   program_des
   hw_des
   results
   conclusions
   appendices

A prominent feature of Cornell's iconic clocktower is the coloring of the
clock faces for special events, which `many students enjoy <https://www.cornellsun.com/article/2025/01/small-sparks-of-wonder-cornell-chimesmasters-brighten-campus-with-clock-face-colors>`_.
However, the current controller for the clocktower is a small remote that
is less than ideal (with unclear controls, variable communication
strength, and lack of extensibility/advanced features).

For our project, we designed a custom LED controller system for the
clocktower. This system uses the existing LED lights, but uses a
distributed system involving 6 Raspberry Pi Pico W's to configure
and drive the LEDs. With the system, Chimesmasters have the ability
to not only set a constant color with more control, but to also have
the lights change with ambient light and volume, and have the ability
to extend the system over WiFi/Bluetooth.

