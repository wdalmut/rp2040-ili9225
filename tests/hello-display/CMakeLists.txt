add_executable(hello_display
	main.c
)

target_compile_options(hello_display PRIVATE -Wall)

target_link_libraries(hello_display
	pico_stdlib
	pico_util
	ili9225
)

# create map/bin/hex file etc.
pico_add_extra_outputs(hello_display)