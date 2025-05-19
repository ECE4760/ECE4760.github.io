# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")


add_executable(TFT_PIO)

pico_enable_stdio_usb(TFT_PIO 1)
pico_enable_stdio_uart(TFT_PIO 0)

pico_generate_pio_header(TFT_PIO ${CMAKE_CURRENT_LIST_DIR}/SPIPIO.pio)

target_sources(TFT_PIO PRIVATE TFT_PIO.c TFTMaster.c glcdfont.c)

target_link_libraries(TFT_PIO PRIVATE 
pico_stdlib 
hardware_gpio 
pico_time 
hardware_pio 
hardware_gpio
hardware_pwm 
hardware_irq
hardware_sync
pico_multicore
hardware_spi
)

pico_add_extra_outputs(TFT_PIO)
