# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# cmake version
cmake_minimum_required(VERSION 3.13)

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(MPU6050_IMU C CXX ASM)

# initialize the sdk
pico_sdk_init()

add_executable(MPU6050_IMU
    imu_demo.cpp
    vga16_graphics_v2.c
    mpu6050.c
    dfPlayer/dfPlayer.cpp
    dfPlayerDriver.h
)

# must match with pio filename and executable name from above
pico_generate_pio_header(MPU6050_IMU ${CMAKE_CURRENT_LIST_DIR}/hsync.pio)
pico_generate_pio_header(MPU6050_IMU ${CMAKE_CURRENT_LIST_DIR}/vsync.pio)
pico_generate_pio_header(MPU6050_IMU ${CMAKE_CURRENT_LIST_DIR}/rgb.pio)

# must match with executable name and source file names
target_sources(MPU6050_IMU PRIVATE imu_demo.cpp vga16_graphics_v2.c mpu6050.c)

# Add BNO08x library
add_subdirectory(BNO08x_Pico_Library)

# Add pico_multicore which is required for multicore functionality
target_link_libraries(MPU6050_IMU pico_stdlib pico_bootsel_via_double_reset pico_multicore hardware_pwm hardware_dma hardware_irq hardware_adc hardware_pio hardware_i2c hardware_clocks BNO08x_Pico_Library hardware_uart)

# # Program metadata
# pico_set_program_name(MPU6050_IMU "pico-dfPlayerMini")
# pico_set_program_version(MPU6050_IMU "0.1")

# # Enable USB serial, disable UART serial
pico_enable_stdio_usb(MPU6050_IMU 1)
pico_enable_stdio_uart(MPU6050_IMU 0)


# create map/bin/hex file etc.
pico_add_extra_outputs(MPU6050_IMU )