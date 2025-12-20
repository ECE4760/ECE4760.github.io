/*
This file is part of peromyscus.

peromyscus is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version.

peromyscus is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
details.

You should have received a copy of the GNU Affero General Public License along
with peromyscus. If not, see <https://www.gnu.org/licenses/>.
*/

#include "hw.h"
#include "global.h"

#include <hardware/irq.h>
#include <hardware/gpio.h>

// toggle the joystick IRQ
void toggle_joystick(void *){
  if (irq_is_enabled(ADC_IRQ_FIFO)){
    irq_set_enabled(ADC_IRQ_FIFO, false);
  } else{
    irq_set_enabled(ADC_IRQ_FIFO, true);
  }
}
// turn the sensor IRQ off
void sensor_off(void *){
  gpio_set_irq_enabled(sensor_config.motion_pin, GPIO_IRQ_LEVEL_LOW,false);
}

// turn the sensor IRQ on
void sensor_on(void *){
  gpio_set_irq_enabled(sensor_config.motion_pin, GPIO_IRQ_LEVEL_LOW,true);
}
