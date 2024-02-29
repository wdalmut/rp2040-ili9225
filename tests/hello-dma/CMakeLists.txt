add_executable(hello_dma
	main.c
)

target_compile_options(hello_dma PRIVATE -Wall)

target_link_libraries(hello_dma
	pico_stdlib
	pico_util
	hardware_dma
	ili9225
)

pico_enable_stdio_usb(hello_dma 1) # enable usb output
pico_enable_stdio_uart(hello_dma 0) # disable uart output

# create map/bin/hex file etc.
pico_add_extra_outputs(hello_dma)