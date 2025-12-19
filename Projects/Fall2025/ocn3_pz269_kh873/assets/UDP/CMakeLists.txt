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
set(PICO_BOARD pico_w CACHE STRING "Board type")

# cmake version
cmake_minimum_required(VERSION 3.13)

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(picow_udp_beacon_background C CXX ASM)

# initialize the sdk
pico_sdk_init()

add_executable(picow_udp_beacon_background)

target_sources(picow_udp_beacon_background PRIVATE picow_udp_beacon.cpp)

target_include_directories(picow_udp_beacon_background PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        )
target_link_libraries(picow_udp_beacon_background
        pico_cyw43_arch_lwip_threadsafe_background
        pico_stdlib pico_bootsel_via_double_reset hardware_sync hardware_spi hardware_dma hardware_adc pico_sync
        )

pico_add_extra_outputs(picow_udp_beacon_background)

