add_executable(mandelbrot-multipico)

# must match with pio filename and executable name from above
pico_generate_pio_header(mandelbrot-multipico ${CMAKE_CURRENT_LIST_DIR}/hsync.pio)
pico_generate_pio_header(mandelbrot-multipico ${CMAKE_CURRENT_LIST_DIR}/vsync.pio)
pico_generate_pio_header(mandelbrot-multipico ${CMAKE_CURRENT_LIST_DIR}/rgb.pio)

# must match with executable name and source file names
target_sources(mandelbrot-multipico PRIVATE mandelbrot_multipico.c vga_graphics.c)

# must match with executable name
target_link_libraries(mandelbrot-multipico PRIVATE pico_stdlib pico_multicore hardware_pio hardware_dma hardware_i2c)

# must match with executable name
pico_add_extra_outputs(mandelbrot-multipico)
