add_executable(DEMO_Agent_Test simple_agent_spi.c)
add_executable(DEMO_Master_SPI spi_test_master.c)
add_executable(DEMO_Slave_SPI spi_test_slave.c)
add_executable(DEMO_Galton galton.c)


target_include_directories(DEMO_Agent_Test PRIVATE ..)
target_include_directories(DEMO_Master_SPI PRIVATE ..)
target_include_directories(DEMO_Slave_SPI PRIVATE ..)
target_include_directories(DEMO_Galton PRIVATE ..)

target_link_libraries(DEMO_Agent_Test PRIVATE 
	pico_stdlib
	pico_bootsel_via_double_reset
	pico_float
	pico_rand
	hardware_pio 
	hardware_dma 
	hardware_adc 
	hardware_sync
	hardware_irq
	pico_multicore
	hardware_clocks
	hardware_vreg
	hardware_spi
	hardware_gpio
	hardware_adc
	driver_vga
	pthreads
)

target_link_libraries(DEMO_Master_SPI PRIVATE 
	pico_stdlib
	pico_bootsel_via_double_reset
	pico_float
	pico_rand
	hardware_pio 
	hardware_dma 
	hardware_adc 
	hardware_sync
	hardware_irq
	pico_multicore
	hardware_clocks
	hardware_vreg
	hardware_spi
	hardware_gpio
	hardware_adc
	driver_vga
	pthreads
)

target_link_libraries(DEMO_Slave_SPI PRIVATE 
	pico_stdlib
	pico_bootsel_via_double_reset
	pico_float
	pico_rand
	hardware_pio 
	hardware_dma 
	hardware_adc 
	hardware_sync
	hardware_irq
	pico_multicore
	hardware_clocks
	hardware_vreg
	hardware_spi
	hardware_gpio
	hardware_adc
	pthreads
)

target_link_libraries(DEMO_Galton PRIVATE 
	pico_stdlib
	pico_bootsel_via_double_reset
	pico_float
	pico_rand
	hardware_pio 
	hardware_dma 
	hardware_adc 
	hardware_sync
	hardware_irq
	pico_multicore
	hardware_clocks
	hardware_vreg
	hardware_spi
	hardware_gpio
	hardware_adc
	driver_vga
	pthreads
)

# must match with executable name
pico_add_extra_outputs(DEMO_Master_SPI)
pico_add_extra_outputs(DEMO_Slave_SPI)
pico_add_extra_outputs(DEMO_Galton)
