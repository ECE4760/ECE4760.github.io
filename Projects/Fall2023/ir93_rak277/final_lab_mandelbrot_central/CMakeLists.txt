add_executable(mandelbrot-multipico-math)


# must match with executable name and source file names
target_sources(mandelbrot-multipico-math PRIVATE mandelbrot_multipico_math.c )

# must match with executable name
target_link_libraries(mandelbrot-multipico-math PRIVATE pico_stdlib pico_multicore hardware_i2c hardware_dma)

# must match with executable name
pico_add_extra_outputs(mandelbrot-multipico-math)
