#include "pico/stdlib.h"
#include "ili9225.h"

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

int main()
{
    // initialize the lcd
    ili9225_init(&lcd_config);

    while (1) {
        // make screen black
        ili9225_fill(0x0000);

        // wait 1 second
        sleep_ms(1000);

        // make screen white
        ili9225_fill(0xffff);

        // wait 1 second
        sleep_ms(1000);
    }
}