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

#include "pmw3389.h"
#include <hardware/gpio.h>
#include "../../global.h"
#include <hardware/spi.h>
#include <string.h>

static uint8_t read_loc[2];
static uint8_t write_loc[2];

// write a register with the correct timings
bool write_pmw_reg(uint8_t addr, const struct pmw3389_cfg_t * cfg, uint8_t data){
  gpio_put(cfg->csn, 0);
  busy_wait_us(t_NCS_SCLK); // delay between NCS and SCLK
  
  write_loc[0] = 0x80 | addr; // set highest bit to indicate write
  write_loc[1] = data;
  auto res = spi_write_blocking(cfg->spi_chan, &write_loc[0], 2);

  busy_wait_us(t_SCLK_NCS_W); // delay between SCLK and NCS being brought high
  gpio_put(cfg->csn, 1);

  busy_wait_us(t_SWx - t_SCLK_NCS_W); // post-transaction delay
  return (res == 2); // make sure two bytes were sent!
}

// read a register with the correct timings.
uint8_t read_pmw_reg(uint8_t addr, const struct pmw3389_cfg_t * cfg){
  gpio_put(cfg->csn, 0);
  busy_wait_us(t_NCS_SCLK);

  write_loc[0] = 0x7f & addr; // clear highest bit to indicate read
  
  spi_write_blocking(cfg->spi_chan, &write_loc[0], 1);
  busy_wait_us(t_SRAD); // read delay
  spi_read_blocking(cfg->spi_chan, 0,&read_loc[0],1);

  busy_wait_us(t_SCLK_NCS_R); // delay between SCLK and NCS being brought high.
  gpio_put(cfg->csn, 1);

  busy_wait_us(t_SRx - t_SCLK_NCS_R); // post-transaction delay

  return read_loc[0];
}

// enale the IRQ again
void reenable(void * e){
  gpio_set_irq_enabled(sensor_config.motion_pin, GPIO_IRQ_LEVEL_LOW,true);
}

// little thing to stich the two unsigned 8-bit ints into a single signed value
// as per how the sensor represents its data.
union conv{
  int16_t val;
  uint8_t b[2];
};

// perform a burst read
void burst_read(void* e){
  // setup destination: 12 bytes are received.
  uint8_t burst_data[12];
  write_pmw_reg(Motion_Burst,&sensor_config,1); // indicate a burst read

  // send the burst address again
  gpio_put(sensor_config.csn, 0);
  write_loc[0] = Motion_Burst;
  spi_write_blocking(sensor_config.spi_chan, &write_loc[0], 1);

  // wait. then, should receive 12 bytes. the 0xff is unimportant.
  busy_wait_us(t_SRAD_motbr);
  spi_read_blocking(sensor_config.spi_chan,0xff,&burst_data[0], 12);

  // raise CS again.
  gpio_put(sensor_config.csn, 1);

  // combine the X and Y measures using the conversion union
  union sensor_data d;
  union conv c = {.b[0] = burst_data[2],.b[1] = burst_data[3]};
  d.delta_x = c.val;

  c.b[0] = burst_data[4];
  c.b[1] = burst_data[5];
  d.delta_y = c.val;

  // allocate space for the driver event, populate its information.
  struct driver_event_t * data = equeue_alloc(&workqueue,event_size);
  data->data = d.raw;
  data->id.source = from_sensor;
  data->id.pin = sensor_config.motion_pin;
  data->id.misc = burst_data[6];

  // post the handler plus the data, and reenable the interrupt in 1ms.
  equeue_post(&workqueue,sensor_config.handler,data);
  equeue_call_in(&workqueue, 1,reenable, nullptr);
}

// IRQ handler for the motion pin
void motion_handler(){
  if (gpio_get_irq_event_mask(sensor_config.motion_pin) == GPIO_IRQ_LEVEL_LOW){
    // acknowledge a motion event, and disable the interrupt for now
    gpio_acknowledge_irq(sensor_config.motion_pin, GPIO_IRQ_LEVEL_LOW);
    gpio_set_irq_enabled(sensor_config.motion_pin, GPIO_IRQ_LEVEL_LOW,false);

    // queue the burst read.
    equeue_call(&workqueue, burst_read, nullptr);
  }
}

bool init_pmw3389(){
  // rate below max from datasheet table 4.3: a bit lower for margin
  spi_init(sensor_config.spi_chan, 2000000);
  spi_set_format(sensor_config.spi_chan,8,SPI_CPOL_1,SPI_CPHA_1,SPI_MSB_FIRST);

  // spi init
  uint32_t spi_mask = (1 << sensor_config.sclk) + (1 << sensor_config.miso) + (1<<sensor_config.mosi);
  gpio_init_mask(spi_mask);
  gpio_set_function_masked(spi_mask, GPIO_FUNC_SPI);


  // cs init
  gpio_init(sensor_config.csn);
  gpio_set_dir(sensor_config.csn, true);
  
  // reset and motion pins
  // TODO: will it save power to have these pulled up?
  //gpio_init(sensor_config.reset_pin);
  //gpio_set_dir(sensor_config.reset_pin, true);

  gpio_init(sensor_config.motion_pin);
  gpio_set_dir(sensor_config.motion_pin, false);
  gpio_pull_up(sensor_config.motion_pin);
  gpio_add_raw_irq_handler(sensor_config.motion_pin, motion_handler);

  // assumes that power sequencing has already taken place

  // reset the chip select for 40us (according to qmk)
  gpio_put(sensor_config.csn, 1);
  busy_wait_us(40);
  gpio_put(sensor_config.csn, 0);
  busy_wait_us(40);
  gpio_put(sensor_config.csn, 1);
  busy_wait_us(40);

  // reset the chip and wait
  write_pmw_reg(Power_Up_Reset,&sensor_config, 0x5a);
  //gpio_put(sensor_config.reset_pin, 1);
  busy_wait_ms(50); // TODO: make async
  //gpio_put(sensor_config.reset_pin, 0);

  // do an initial set of reads, discarding results
  read_pmw_reg(Motion, &sensor_config);
  read_pmw_reg(Delta_X_L,&sensor_config);
  read_pmw_reg(Delta_X_H, &sensor_config);
  read_pmw_reg(Delta_Y_L, &sensor_config);
  read_pmw_reg(Delta_Y_H, &sensor_config);
  

  // TODO: srom download?
  //gpio_put(sensor_config.reset_pin, 1);
  busy_wait_ms(10);
  // write_pmw_reg(Res_L, &sensor_config, 200);
  // write_pmw_reg(Res_H, &sensor_config, 0);

  // TODO: CPI configuration?

  return true;
}

// enable the IRQ for the motion pin.
bool start_pmw3389(){
  gpio_set_irq_enabled(sensor_config.motion_pin, GPIO_IRQ_LEVEL_LOW,true);
  irq_set_enabled(IO_IRQ_BANK0, true);
  return true;
}
