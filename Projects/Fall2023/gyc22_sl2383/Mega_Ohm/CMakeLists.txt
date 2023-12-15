add_executable(resistor_project)

# must match with pio filename and executable name from above
pico_generate_pio_header(resistor_project ${CMAKE_CURRENT_LIST_DIR}/SPIPIO.pio)

# must match with executable name and source file names
target_sources(resistor_project PRIVATE resistor.c TFTMaster.c LED.c cycle_delay.S)

# Add pico_multicore which is required for multicore functionality
target_link_libraries(resistor_project pico_stdlib hardware_spi pico_multicore hardware_pio hardware_irq hardware_pwm hardware_dma hardware_adc)

# create map/bin/hex file etc.
pico_add_extra_outputs(resistor_project)