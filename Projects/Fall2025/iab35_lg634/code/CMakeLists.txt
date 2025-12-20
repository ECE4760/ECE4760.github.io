set(PICO_BOARD pico CACHE STRING "Board type")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(TINYUSB_FAMILY_PROJECT_NAME_PREFIX "tinyusb_dev_")


# cmake version
cmake_minimum_required(VERSION 3.13)

set(TOP ${PICO_TINYUSB_PATH})
set(FAMILY rp2040)
set(BOARD pico_sdk)
# include the sdk.cmake file
include(pico_sdk_import.cmake)
#include("${PICO_SDK_PATH}/lib/tinyusb/src/CMakeLists.txt")

# give the project a name (anything you want)
project(mouse C CXX ASM)

# initialize the sdk
pico_sdk_init()

add_executable(mouse)
set_property(TARGET mouse PROPERTY C_STANDARD 23)
#add_tinyusb(mouse)
#message("${PICO_SDK_PATH}")

target_include_directories(mouse PUBLIC
  .
  drivers/
  drivers/pmw3389/
  drivers/button/
  drivers/encoder/
  drivers/joystick/
  equeue/
  events/
)

# must match with executable name and source file names
target_sources(mouse PUBLIC
  main.c
  usb_descriptors.c
  global.c
  drivers/pmw3389/pmw3389.c
  drivers/button/button.c
  drivers/encoder/encoder.c
  drivers/joystick/joystick.c
  equeue/equeue.c
  equeue/equeue_pico.c
  events/ebind.c
  events/config.c
  events/config_func.c
  events/elib.c
  events/hw.c
)

# Add pico_multicore which is required for multicore functionality
target_link_libraries(mouse PUBLIC
  pico_stdlib
  pico_bootsel_via_double_reset
  pico_unique_id
  hardware_spi
  hardware_adc
  tinyusb_device
  tinyusb_board
)

# create map/bin/hex file etc.
pico_add_extra_outputs(mouse)
