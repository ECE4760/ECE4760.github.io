# cmake version
cmake_minimum_required(VERSION 3.12)

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(lwip_testing)
#set(CMAKE_C_STANDARD 11)
#set(CMAKE_CXX_STANDARD 17)

# initialize the sdk
if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(PICO_BOARD pico_w)
#set(WIFI_SSID ****)
#set(WIFI_PASSWORD ****)

pico_sdk_init()

#######

# name anything you want
add_executable(lwip_test)

# must match with executable name and source file names
target_sources(lwip_test PRIVATE 
	picow_udp_send_recv_data.c
)

target_include_directories(lwip_test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
)

# must match with executable name
target_link_libraries(lwip_test PRIVATE 
	pico_cyw43_arch_lwip_threadsafe_background 
	pico_cyw43_arch
	
	pico_stdlib 
	pico_bootsel_via_double_reset 

	hardware_pwm
	hardware_pio 
	hardware_dma 
	hardware_adc 
	hardware_sync
	hardware_irq
	hardware_gpio
	hardware_spi
    hardware_clocks
	hardware_rtc
	pico_multicore
)

# must match with executable name
pico_add_extra_outputs(lwip_test)

add_compile_options(-O3)

#  Choose Build or Build Solution, or press Ctrl+Shift+B  #
