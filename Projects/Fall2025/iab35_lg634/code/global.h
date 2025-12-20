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


#ifndef GLOBAL_H
#define GLOBAL_H

/*
  this header exports the important hardware configuration structs from the .c file in which they're defined.
  it also includes the workqueue so drivers can schedule work from 'outside'.
*/

#include "equeue/equeue.h"
#include <assert.h>
#include <stdint.h>

#include "drivers/button/button.h"
#include "drivers/encoder/encoder.h"
#include "drivers/pmw3389/pmw3389.h"
#include "drivers/joystick/joystick.h"

#include "events/ebind.h"

// references to hardware configurations
extern const struct button_t button_configs[];
extern const struct encoder_t encoder_configs[];
extern const struct pmw3389_cfg_t sensor_config;

extern const struct joystick_t joystick_config;

// unfortunately couldn't figure out a way to make these constexprs
extern const size_t num_buttons;
extern const size_t num_encoders;



extern equeue_t workqueue;


#endif
