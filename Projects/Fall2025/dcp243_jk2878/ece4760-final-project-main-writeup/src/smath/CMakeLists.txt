add_library(smath STATIC
	fix.h
	fix.c
	linalg.h
	linalg.c
)

# Only share relevant dependencies 
target_link_libraries(smath
	PRIVATE 
		pico_stdlib
		pico_stdio
	PUBLIC 
		pico_float
		pico_double
		pico_rand
)
