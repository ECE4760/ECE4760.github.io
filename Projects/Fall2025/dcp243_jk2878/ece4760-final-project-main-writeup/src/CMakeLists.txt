## INIT ##

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")


## PROJECT SETUP ##

# cmake version
cmake_minimum_required(VERSION 3.13)

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(Pico_Snake 
	DESCRIPTION 
		"Multiplayer Snake-inspired game for the Pi Pico"
	LANGUAGES
		C CXX ASM)


## GLOBAL PROJECT CONFIG ##

# initialize the sdk
pico_sdk_init()

set(PICO_DEFAULT_FLOAT_IMPL pico)
set(PICO_DEFAULT_DOUBLE_IMPL pico)

# For code linting tools/IDE
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compile-time configs
set(PICO_USE_STACK_GUARDS)
set(USE_PICO_FIX TRUE)


## LIBRARY SUBDIRECTORIES ##
add_subdirectory(smath)
add_subdirectory(driver)

## GLOBAL PROJECT INTERFACES ##
add_library(pthreads INTERFACE)
target_sources(pthreads INTERFACE pt_cornell_rp2040_v1_4.h)

add_library(scomm INTERFACE)
target_link_libraries(scomm INTERFACE smath)
target_sources(scomm INTERFACE snake_state.h)


## TEST SUBDIRECTORIES ##
# TODO: Add tests

## DEMO CODE SUBDIRECTORIES ##
#add_subdirectory(demo)

## CONFIG ##
add_subdirectory(config)


## MAIN EXECUTABLE ##
add_executable(Pico_Snake)

# Source files
target_sources(Pico_Snake PUBLIC snake_server.c snake_state.c snake_state.h)

# Additional PIO files

# Main options
pico_set_float_implementation(Pico_Snake pico)
pico_set_double_implementation(Pico_Snake pico)

target_link_options(Pico_Snake PRIVATE -Wl,--cref)

target_compile_definitions(Pico_Snake PRIVATE SERVER_INSTANCE)

# Dependencies
target_link_libraries(Pico_Snake PRIVATE
	# SDK
	pico_stdlib
	pico_float
	pico_rand
	pico_multicore
	pico_bootsel_via_double_reset
	hardware_pio
	hardware_dma
	hardware_adc
	hardware_sync
	hardware_irq
	hardware_clocks
	hardware_vreg
	hardware_spi
	hardware_gpio
	# Custom libs
	pthreads
	driver_vga
	smath
	scomm
	# Configs
	config_game
)

# Extra outputs from build (.bin, .map, UF2, etc.)
pico_add_extra_outputs(Pico_Snake)
