/**
 * Copyright (C) 2019-2022 by Mahyar Koshkouei <mk@deltabeard.com>
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _MK_ILI9225_H
#define _MK_ILI9225_H

#include "hardware/spi.h"

struct ili9225_config {
    spi_inst_t* spi;
    uint gpio_din;
    uint gpio_clk;
    uint gpio_cs;
    uint gpio_rs;
    uint gpio_rst;
    uint gpio_bl;
    uint gpio_led;
};

//#define NDEBUG
#ifndef MK_ILI9225_READ_AVAILABLE
# define MK_ILI9225_READ_AVAILABLE 0
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define SCREEN_SIZE_X 176u
#define SCREEN_SIZE_Y 220u

typedef enum {
	ILI9225_COLOR_MODE_FULL = 0,
	ILI9225_COLOR_MODE_8COLOR = 1
} ili9225_color_mode_e;

/**
 * Controls the reset pin of the ILI9225.
 * \param state	Set to 0 on low output, else high.
 */
void ili9225_set_rst(bool state);

/**
 * Controls state of RS pin.
 * \param state	Set to 0 on low output, else high.
 */
void ili9225_set_rs(bool state);

/**
 * Controls state of CS pin.
 * \param state	Set to 0 on low output, else high.
 */
void ili9225_set_cs(bool state);

/**
 * Controls state of LED pin.
 * \param state	Set to 0 on low output, else high.
 */
void ili9225_set_led(bool state);

/**
 * Sends data to the ILI9225 using SPI. Return only after sending data.
 * \param halfword Data to send.
 */
void ili9225_spi_write16(const uint16_t *halfwords, size_t len);

#if MK_ILI9225_READ_AVAILABLE
/**
 * Reads 16-bit data from the ILI9225 using SPI.
 * \return 16-bit data.
 */
extern uint16_t ili9225_spi_read16(void);
#endif

/**
 * Delays execution in milliseconds.
 * \param ms Duration to sleep for.
 */
void ili9225_delay_ms(unsigned ms);

/**
 * Initialise ILI9225 LCD with default settings.
 * \return 0 on success, else error due to invalid LCD identification.
 */
unsigned ili9225_init(const struct ili9225_config*);

#if MK_ILI9225_READ_AVAILABLE
/**
 * Read the current line being driven by the LCD. Can help with tearing
 * mitigation.
 * \return Line driven by LCD.
 */
unsigned ili9225_read_driving_line(void);
#endif

/**
 * Set the window that pixel will be written to. Address will loop within the
 * window.
 *
 * \param hor_start
 * \param hor_end
 * \param vert_start
 * \param vert_end
 */
void ili9225_set_window(uint16_t hor_start, uint16_t hor_end,
	uint16_t vert_start, uint16_t vert_end);

/**
 * Set address pointer in GRAM. Must be within window.
 * \param x
 * \param y
 */
void ili9225_set_address(uint8_t x, uint8_t y);

/**
 * Write pixels to ILI9225 GRAM. These pixels will be displayed on screen at
 * next vsync.
 * \param pixels Pixel data in RGB565 format to write to LCD.
 * \param nmemb Number of pixels.
 */
void ili9225_write_pixels(const uint16_t *pixels, uint_fast16_t nmemb);

/**
 * Inverts the display.
 * @param invert
 */
void ili9225_invert_display(bool invert);

void ili9225_write_pixels_start(void);
void ili9225_write_pixels_end(void);

void ili9225_set_gate_scan(uint16_t hor_start, uint16_t hor_end);

void ili9225_display_control(bool invert, ili9225_color_mode_e colour_mode);

void ili9225_power_control(uint8_t drive_power, bool sleep);

void ili9225_set_drive_freq(uint16_t f);

void ili9225_set_x(uint8_t x);

/**
 * Exit and stop using LCD. Currently does nothing.
 */
void ili9225_exit(void);

/**
 * Fill a rectangle at the given location, size and color.
 */
void ili9225_fill_rect(uint8_t x,uint8_t y,uint8_t w,uint8_t h,uint16_t color);

/**
 * Fill the entire screen with the specified RGB565 color
 */
void ili9225_fill(uint16_t color);

/**
 * Set a specified pixel to the given color
 */
void ili9225_pixel(uint8_t x,uint8_t y,uint16_t color);

/**
 * Copy the specified framebuffer at the given coordinates
 */
void ili9225_blit(uint16_t *fbuf,uint8_t x,uint8_t y,uint8_t w,uint8_t h);

/**
 * Return an 8x8 framebuffer for the given letter and color / background color
 */
void ili9225_get_letter(uint16_t *fbuf,char letter,uint16_t color,uint16_t bgcolor);

/**
 * Write text to the screen using the the coordinates as the upper-left corner of the text.
 * All characters have dimensions of 8x8 pixels.
 */
void ili9225_text(char *s,uint8_t x,uint8_t y,uint16_t color,uint16_t bgcolor);

#endif