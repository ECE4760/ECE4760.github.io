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


#ifndef EVENT_HW_H
#define EVENT_HW_H


// the sensor cpi can be changed, but we're not confident enough in it...
// these do about what they say in the name.
void toggle_joystick(void *);
void sensor_off(void *);
void sensor_on(void *);

#endif
