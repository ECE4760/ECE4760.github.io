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
#ifndef PMW3389_D
#define PMW3389_D
#include <hardware/spi.h>
#include <hardware/gpio.h>
#include <hardware/timer.h>
#include "../driver.h"



/*
  hardware config:
  - which SPI peripheral
  - which pins

  it is the user's responsibility to determine which pins correspond to the SPI peripheral
*/
struct pmw3389_cfg_t{
  spi_inst_t * spi_chan;
  pin_no csn;
  pin_no sclk;

  //! note that these are the opposite of the chip side!
  pin_no miso;
  pin_no mosi;
  
  pin_no reset_pin; // output: reset pin
  pin_no motion_pin; // input: motion interrupt pin

  void (*handler);
};

// initialise hardware:
// configure GPIO pins, reserve the spi channel
bool init_pmw3389();

// actually start transmissions by writing to the correct reg
bool start_pmw3389();

uint8_t read_pmw_reg(uint8_t addr, const struct pmw3389_cfg_t * cfg);

bool write_pmw_reg(uint8_t addr, const struct pmw3389_cfg_t * cfg, uint8_t data);


/*
  timing constants.
  all converted to us

*/
// SWR and SWX both use this value
#define t_SWx 180

// SRR and SRW both use this value
#define t_SRx 20

#define t_SRAD 160
#define t_SRAD_motbr 35

#define t_SCLK_NCS_W 35

// motion delay: 50ms
#define t_MOTDEL 50000

// below were converted from ns
// since the values are so small, they are rounded up

// 500ns -> 1us
#define t_BEXIT 1
// 120 ns -> 1us
#define t_NCS_SCLK 1
#define t_SCLK_NCS_R 1

/*
  software config:
  - whether to load firmware
  - programmable invert? (CTRL1)
  - config: resolution
  - rest mode
  - angle_tune
  - downshift timings
  - angle snap?
  - lift config?
  
*/

#define PMW3389_USE_FW


/*
  registers
*/

enum pmw3389_regs: uint8_t{
 Product_ID = 0x00,
 Revision_ID = 0x01,
 Motion = 0x02,
 // the below should normally be read in burst mode
 Delta_X_L,
 Delta_X_H,
 Delta_Y_L,
 Delta_Y_H,
 SQUAL,
 RawData_Sum,
 Max_RawData,
 Min_RawData,
 Shutter_Lower,
 Shutter_Upper,
 Ripple_Ctrl,
 Res_L,
 Res_H,
 
 Config2 = 0x10,
 Angle_Tune,
 Frame_Capture,
 SROM_Enable,
 Run_Downshift,
 Rest1_Rate_L,
 Rest1_Rate_U,
 Rest1_Downshift,
 Rest2_Rate_L,
 Rest2_Rate_U,
 Rest2_Downshift,
 Rest3_Rate_L,
 Rest3_Rate_U,

 Observ = 0x24,
 Data_Out_L,
 Data_Out_U,

 SROM_ID = 0x2A,
 Min_SQ_run,
 RawData_Thres,
 Control2,
 Config5_L,
 Config5_H,

 Power_Up_Reset = 0x3A,
 Shutdown,

 I_Product_ID = 0x3F,
 LiftCutoff_Cal3 = 0x41,
 Angle_Snap = 0x42,
 LiftCutoff_Cal1 = 0x4a,
 Motion_Burst = 0x50,
 SROM_Load_Burst = 0x62,
 Lift_Config = 0x63,
 RawData_Burst = 0x64,
 LiftCutoff_Cal2 = 0x65,
 LiftCutoff_Cal_Timeout = 0x71,
 LiftCutoff_Cal_Min_Len = 0x72,
 PWM_Period_Cnt = 0x73,
 PWM_Width_Cnt = 0x74
};


#endif
