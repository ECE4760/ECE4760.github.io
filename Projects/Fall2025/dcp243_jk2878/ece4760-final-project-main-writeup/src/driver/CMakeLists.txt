# `driver` is just a container for our actual driver code

# Pull in all subdirectories and use their CMakeLists
add_subdirectory(vga)

add_library(driver_vga INTERFACE)
target_link_libraries(driver_vga INTERFACE vga)