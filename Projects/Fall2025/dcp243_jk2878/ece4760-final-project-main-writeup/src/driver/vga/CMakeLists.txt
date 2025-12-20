add_library(vga STATIC
	font_rom_brl4.h
	glcdfont.c
	vga16_graphics_v2.h
	vga16_graphics_v2.c
)

# must match with pio filename and executable name from above
pico_generate_pio_header(vga ${CMAKE_CURRENT_LIST_DIR}/hsync.pio)
pico_generate_pio_header(vga ${CMAKE_CURRENT_LIST_DIR}/vsync.pio)
pico_generate_pio_header(vga ${CMAKE_CURRENT_LIST_DIR}/rgb.pio)

# must match with executable name
target_link_libraries(vga PRIVATE
	pico_stdlib
	pico_stdio
	hardware_gpio
	hardware_pio
	hardware_dma
)