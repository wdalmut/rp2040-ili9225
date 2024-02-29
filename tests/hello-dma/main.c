#include "pico/stdlib.h"
#include "ili9225.h"
#include <stdio.h>

// lcd configuration
const struct ili9225_config lcd_config = {
    .spi      = spi0,
    .gpio_din = 19,
    .gpio_clk = 18,
    .gpio_cs  = 17,
    .gpio_rs  = 20,
    .gpio_rst = 21,
    .gpio_led = 22
};


const int lcd_width = 220;
const int lcd_height = 176;

void my_display_flush(void)
{
    printf("display data flush completed.\n");
}

int main()
{
    stdio_init_all();

    uint16_t buffer[lcd_width * lcd_height];
    
    sleep_ms(3000);

    // initialize the lcd
    ili9225_init(&lcd_config);

    // initialize the lcd
    ili9225_set_dma_irq_handler(DMA_IRQ_0, my_display_flush);

    while (1) {
        for (int i = 0; i < lcd_width * lcd_height; i++) {
            buffer[i] = 0x0000;
        }
        
        ili9225_set_address(0,0);
        ili9225_dma_write(buffer, lcd_width * lcd_height);

        // wait 1 second
        sleep_ms(1000);
        printf("sleep 1s\n");

        for (int i = 0; i < lcd_width * lcd_height; i++) {
            buffer[i] = 0xFFFF;
        }
        
        ili9225_set_address(0,0);
        ili9225_dma_write(buffer, lcd_width * lcd_height);

        // wait 1 second
        sleep_ms(1000);
        printf("sleep 1s\n");
    }
}