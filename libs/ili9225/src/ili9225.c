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

#include <assert.h>

#include <hardware/clocks.h>
#include <hardware/spi.h>
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/dma.h"
#include "pico/time.h"

#include "ili9225.h"

/* Register Descriptions. */
/**
 * The index register (IR) stored the register address.
 * The register selection signal (RS) selects register access.
 * The read/write signals (nRD/nWR) select read or write command, respectively.
 * The data bus D0-D17 are used to read or write data. Register access is
 * 16-bit, so 18-bit bus is reduced to 16-bit only (see page 45).
 * On writing data GRAM, the internal address is automatically incremented.
 */

/* ILI9225 Registers. */
/**
 * [Register Name] ([RO,WO, or WR])
 * R ([Read mask]): [Description of what can be read at mask]
 * W ([Write mask]): [Description of what can be written at mask]
 */
/**
 * Driver Code Read (RW)
 * R (0xFFFF): Reads the device code integer 0x9225.
 * W: (0xXXX1): Starts (0x1) or stops (0x0) internal oscillator.
 * At least 10ms must pass after starting the internal oscillator to allow it
 * to stabilise.
 */
#define MK_ILI9225_REG_DRIVER_CODE_READ		0x00
#define MK_ILI9225_REG_START_OSCILLATION	0x00
/**
 * Driver Output Control (WO)
 * W (0x8XXX): VSPL: Inverts the polarity of VSYNC pin. "1" is high active.
 * W (0x4XXX): HSPL: Inverts the polarity of HSYNC pin. "1" is high active.
 * W (0x2XXX): DPL: Same as above for DOTCLK.
 * W (0x1XXX): EPL: Same as above for ENABLE.
 * W (0xX4XX): SM: Scan order by gate driver.
 * W (0xX2XX): GS: Shift direction of gate driver.
 * W (0xX1XX): SS: Shift direction from source driver.
 * W (0xXX1F): NL: Set active gate driver line. Default: 0b11011.
 */
#define MK_ILI9225_REG_DRIVER_OUTPUT_CTRL	0x01
/**
 * LCD Driving Waveform Control (WO)
 * Set LCD inversion method.
 * W (0xX3XX): INV: Sets inversion method.
 * W (0xXXX1): FLD: 3-field interlaced scanning function.
 */
#define MK_ILI9225_REG_LCD_AC_DRIVING_CTRL	0x02
/**
 * Entry Mode (WO)
 * W (0x1XXX): BGR: Swap Red with Blue.
 * W (0xX3XX): MDT: Data transmission format for 80-mode.
 * W (0xXX2X): ID1: Control horizontal address counter increment (1) or
 *		decrement (0).
 * W (0xXX1X): ID0: Control vertical address counter increment (1) or
 *		decrement (0).
 * W (0xXXX8): AM: GRAM update direction. "1" is horizontal.
 */
#define MK_ILI9225_REG_ENTRY_MODE		0x03
/**
 * Display Control 1 (WO)
 * W (0x1XXX): TEMON: Sets the frame flag output signal FLM.
 * W (0xXX1X): GON: Set output level of gate driver. VGL = 0, Normal = 1.
 * W (0xXXX8): CL: Selects 8-colour display mode when 1. Else 2^18 colours.
 * W (0xXXX4): REV: Reverses greyscale levels if set.
 * W (0xXXX3): D: Control display output. 0b11 to turn on display, else display
 *		is off.
 */
#define MK_ILI9225_REG_DISPLAY_CTRL		0x07
/**
 * Display Control 2 (WO)
 * Used for I80, M68, and RGB modes only.
 * W (0xXFXX): FP: Specify number of front porch lines.
 * W (0xXXXF): BP: Specify number of back porch lines.
 */
#define MK_ILI9225_REG_BLANK_PERIOD_CTRL	0x08
/**
 * Frame Cycle Control (WO)
 * Used for RGB mode only.
 * W (0xFXXX): NO: Set amount of non-overlay for gate output.
 * W (0xXFXX): SDT: Set delay from gate edge to source output.
 * W (0xXXXF): RTN: Set number of clock cycles for one display line.
 */
#define MK_ILI9225_REG_FRAME_CYCLE_CTRL		0x0B
/**
 * RGB Input Interface Control (W)
 * Used for RGB mode only.
 */
#define MK_ILI9225_REG_INTERFACE_CTRL		0x0C
/**
 * Oscillator Control (W)
 * W (0xXFXX): FOSC: Oscillation frequency.
 * W (0xXXX1): OSC_EN: Starts oscillation from halt state. 10ms wait required.
 */
#define MK_ILI9225_REG_OSC_CTRL			0x0F
/**
 * Power Control 1 (W)
 * W (0xXFXX): SAP: Set current level of driver.
 * W (0xXXX2): DSTB: Enable deep standby mode. Writing to GRAM is prohibited.
 * W (0xXXX1): STB: Enable sleep mode. Stops display and internal oscillator.
 */
#define MK_ILI9225_REG_PWR_CTRL1		0x10
/**
 * Power Control 2 (W)
 * W (0x1XXX): APON: Use automatic boosting operation.
 * W (0xXFXX): PON: Set boosting operation for specific circuits.
 * W (0xXX2X): AON: Set operation of amplifier.
 * W (0xXX1X): VCL1EN: Set operation of generation amplifier.
 * W (0xXXXF): VC: Set VCl1 voltage output.
 */
#define MK_ILI9225_REG_PWR_CTRL2		0x11
/**
 * Power Control 3 (W)
 * W (0x7XXX): BT: Set output factor of boost converter. 
 * W (0xX7XX): DC1: Operation frequency of circuit 1 boost converter.
 * W (0xXX7X): DC2: Operation frequency of circuit 2 boost converter.
 * W (0XXXX7): DC3: Operation frequency of circuit 3 boost converter.
 */
#define MK_ILI9225_REG_PWR_CTRL3		0x12
/**
 * Power Control 4 (W)
 * W (0xXX7F): GVD: Set gamma voltage (GVDD) from 2.66V to 5.5V.
 */
#define MK_ILI9225_REG_PWR_CTRL4		0x13
/**
 * Power Control 5 (W)
 * W (0x8XXX): VCOMG: Sets amplitude of VCOM signal.
 * W (0x7FXX): VCM: Sets VCOMH voltage, from 0.4015 to 1.1000 * GVDD.
 * W (0xXX7F): VML: Sets alternate amplitudes of VCOM from 0.534 to 1.200*GVDD.
 */
#define MK_ILI9225_REG_PWR_CTRL5		0x14
/**
 * VCI Recycling (W)
 * W (0xXX7X): VCIR: Number of clock cycles in VCI recycling period.
 */
#define MK_ILI9225_REG_VCI_RECYCLING		0x15
/**
 * RAM Address Set 1 (W)
 * W (0xXXFF): AD[7:0]: Set the initial value of the address counter.
 */ 
#define MK_ILI9225_REG_RAM_ADDR_SET1		0x20
/**
 * RAM Address Set 2 (W)
 * W (0xXXFF): AD[15:8]: Set the initial value of the address counter.
 */ 
#define MK_ILI9225_REG_RAM_ADDR_SET2		0x21
/**
 * Read/Write GRAM Data (RW)
 * W (0x3FFFF): Write data to GRAM.
 * R (0x3FFFF): Read data from GRAM.
 */
#define MK_ILI9225_REG_GRAM_RW			0x22
/**
 * Software Reset (W)
 * W (0xFFFF): Perform software reset when 0x00CE is written.
 */
#define MK_ILI9225_REG_SOFT_RESET		0x28
/**
 * Gate Scan Control (W)
 * W (0xXX1F): SCN: Line to start gate scan from.
 */
#define MK_ILI9225_REG_GATE_SCAN_CTRL		0x30
/**
 * Vertical Scroll Control 1 (W)
 * W (0xXXFF): SEA: Scroll end address.
 */
#define MK_ILI9225_REG_VERT_SCROLL_CTRL1	0x31
/**
 * Vertical Scroll Control 2 (W)
 * W (0xXXFF): SSA: Scroll start address.
 */
#define MK_ILI9225_REG_VERT_SCROLL_CTRL2	0x32
/**
 * Vertical Scroll Control 3 (W)
 * W (0xXXFF): SST: Scroll step.
 */
#define MK_ILI9225_REG_VERT_SCROLL_CTRL3	0x33
/**
 * Partial Screen Driving Position (W)
 * W (0xXXFF): SE: High byte of screen end position.
 */
#define MK_ILI9225_REG_PART_DRIVING_POS1	0x34
/**
 * Partial Screen Driving Position (W)
 * W (0xXXFF): SS: High byte of screen start position.
 */
#define MK_ILI9225_REG_PART_DRIVING_POS2	0x35
/**
 * Horizontal RAM Address Position (W)
 * W (0xXXFF): HEA: End horizontal position.
 */
#define MK_ILI9225_REG_HORI_WIN_ADDR1		0x36
/**
 * Horizontal RAM Address Position (W)
 * W (0xXXFF): HSA: Start horizontal position.
 */
#define MK_ILI9225_REG_HORI_WIN_ADDR2		0x37
/**
 * Vertical RAM Address Position (W)
 * W (0xXXFF): VEA: End vertical position.
 */
#define MK_ILI9225_REG_VERT_WIN_ADDR1		0x38
/**
 * Vertical RAM Address Position (W)
 * W (0xXXFF): VSA: Start vertical position.
 */
#define MK_ILI9225_REG_VERT_WIN_ADDR2		0x39
/**
 * Gamma Control (W)
 */
#define MK_ILI9225_REG_GAMMA_CTRL1		0x50
#define MK_ILI9225_REG_GAMMA_CTRL2		0x51
#define MK_ILI9225_REG_GAMMA_CTRL3		0x52
#define MK_ILI9225_REG_GAMMA_CTRL4		0x53
#define MK_ILI9225_REG_GAMMA_CTRL5		0x54
#define MK_ILI9225_REG_GAMMA_CTRL6		0x55
#define MK_ILI9225_REG_GAMMA_CTRL7		0x56
#define MK_ILI9225_REG_GAMMA_CTRL8		0x57
#define MK_ILI9225_REG_GAMMA_CTRL9		0x58
#define MK_ILI9225_REG_GAMMA_CTRL10		0x59

#define MK_ILI9225_NV_MEM_DATA_PROG		0x60
#define MK_ILI9225_NV_MEM_CTRL			0x61
#define MK_ILI9225_NV_MEM_STAT			0x62
#define MK_ILI9225_NV_MEM_PROTECTION_KEY	0x63
#define MK_ILI9225_NV_MEM_ID_CODE		0x65

#define MK_ILI9225_REG_MTP_TEST_KEY		0x80
#define MK_ILI9225_REG_MTP_CTRL_REG		0x81
#define MK_ILI9225_REG_MTP_DATA_READ		0x82

/* Useful macros. */
#define ARRAYSIZE(array)    (sizeof(array)/sizeof(array[0]))

static struct ili9225_config ili9225_cfg;

/* Structure definitions. */
struct reg_dat_pair {
	uint16_t reg;
	uint16_t dat;
};

/* Local functions. */
static void write_register(uint16_t cmd);
static void write_data(uint16_t dat);
#if MK_ILI9225_READ_AVAILABLE
static uint16_t read_data(void);
static uint16_t get_register(uint16_t reg);
#endif

static int dma_irq;
static uint dma_tx;
static dma_channel_config c;

static ili9225_dma_finish_callback_t f_dma_finish_callback;

static void _ili9225_dma_finish_callback(void)
{
	ili9225_write_pixels_end();

    if (dma_irq == DMA_IRQ_0) {
        dma_channel_set_irq0_enabled(dma_tx, true);
        dma_hw->ints0 = 1u << dma_tx;
    } else if (dma_irq == DMA_IRQ_1) {
        dma_channel_set_irq0_enabled(dma_tx, true);
        dma_hw->ints1 = 1u << dma_tx;
    } else {
        return;
    }
    

    f_dma_finish_callback();
}

void ili9225_set_dma_irq_handler(uint num, ili9225_dma_finish_callback_t cb)
{
    if (num == DMA_IRQ_0) {
        dma_channel_set_irq0_enabled(dma_tx, true);
    } else if (num == DMA_IRQ_1) {
        dma_channel_set_irq1_enabled(dma_tx, true);
    } else {
        return;
    }
    
    dma_irq = num;

    f_dma_finish_callback = cb;

    irq_set_exclusive_handler(num, _ili9225_dma_finish_callback);
    irq_set_enabled(num, true);
}

static void write_register(uint16_t cmd)
{
	ili9225_set_rs(0);
	ili9225_set_cs(0);
	ili9225_spi_write16(&cmd, 1);
	ili9225_set_cs(1);
}

static void write_data(uint16_t dat)
{
	ili9225_set_rs(1);
	ili9225_set_cs(0);
	ili9225_spi_write16(&dat, 1);
	ili9225_set_cs(1);
}

#if MK_ILI9225_READ_AVAILABLE
static uint16_t read_data(void)
{
	uint16_t ret;
	ili9225_set_rs(1);
	ili9225_set_cs(0);
	ret = ili9225_spi_read16();
	ili9225_set_cs(1);
	return ret;
}

static uint16_t get_register(uint16_t reg)
{
	write_register(reg);
	return read_data();
}
#endif

static void set_register(uint16_t reg, uint16_t dat)
{
	write_register(reg);
	write_data(dat);
}

#if MK_ILI9225_READ_AVAILABLE
unsigned ili9225_read_driving_line(void)
{
	unsigned line;
	ili9225_set_rs(0);
	ili9225_set_cs(0);
	line = read_data();
	ili9225_set_cs(1);
	return (line >> 8);
}
#endif

/* Public functions. */
unsigned ili9225_init(const struct ili9225_config *config)
{
	{
		memcpy(&ili9225_cfg, config, sizeof(ili9225_cfg));
		
		gpio_set_function(ili9225_cfg.gpio_din, GPIO_FUNC_SPI);
    	gpio_set_function(ili9225_cfg.gpio_clk, GPIO_FUNC_SPI);

		gpio_set_function(ili9225_cfg.gpio_cs, GPIO_FUNC_SIO);
		gpio_set_function(ili9225_cfg.gpio_clk, GPIO_FUNC_SPI);
		gpio_set_function(ili9225_cfg.gpio_din, GPIO_FUNC_SPI);
		gpio_set_function(ili9225_cfg.gpio_rs, GPIO_FUNC_SIO);
		gpio_set_function(ili9225_cfg.gpio_rst, GPIO_FUNC_SIO);
		gpio_set_function(ili9225_cfg.gpio_led, GPIO_FUNC_SIO);

		gpio_set_dir(ili9225_cfg.gpio_cs, true);
		gpio_set_dir(ili9225_cfg.gpio_rs, true);
		gpio_set_dir(ili9225_cfg.gpio_rst, true);
		gpio_set_dir(ili9225_cfg.gpio_led, true);
		gpio_set_slew_rate(ili9225_cfg.gpio_clk, GPIO_SLEW_RATE_FAST);
		gpio_set_slew_rate(ili9225_cfg.gpio_din, GPIO_SLEW_RATE_FAST);

		/* Set SPI clock to use high frequency. */
		clock_configure(clk_peri, 0,
			CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
			125 * 1000 * 1000, 125 * 1000 * 1000);
		spi_init(ili9225_cfg.spi, 30*1000*1000);
		spi_set_format(ili9225_cfg.spi, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
	}

	unsigned ret = 0x9225;

	/* Initialise reset pin as not reset. RST must be high before reset. */
	ili9225_set_rst(1);
	/* Initialise other pins too. */
	ili9225_set_cs(1);
	ili9225_set_rs(0);
	/* Make sure pin is initialised by setting small delay. */
	ili9225_delay_ms(1);

	/* Put ILI9225 into reset. */
	ili9225_set_rst(0);
	/* Reset low-level width (Tres) is at least 1ms. */
	ili9225_delay_ms(10);

	/* Bring ILI9225 out of reset. */
	ili9225_set_rst(1);
	ili9225_delay_ms(50);
	
	/* Turn backlight off initially */
	ili9225_set_led(0);

	/* Initialise power registers. */
	/* FIXME: This may not be required. All are initialised to 0x00,
	 * FIXME: but VCOMG is set to 1 on init apparently. */
	{
		const uint8_t regs[] = {
			MK_ILI9225_REG_PWR_CTRL1, MK_ILI9225_REG_PWR_CTRL2,
			MK_ILI9225_REG_PWR_CTRL3, MK_ILI9225_REG_PWR_CTRL4,
			MK_ILI9225_REG_PWR_CTRL5
		};

		for(uint8_t i = 0; i < sizeof(regs); i++)
			set_register(regs[i], 0x0000);
	}
	ili9225_delay_ms(40);

	/* Switch on power control. */
	{
		/* VCI set to 2.58V. */
		/* FIXME: CTRL3 VGH is set to 2.58*6=15.48, but it could be
		 * FIXME: lower ( 9 < VGH < 16.5).
		 * FIXME: CTRL3 VGL is set to 2.58*-4=-10.32, but it could be
		 * FIXME: higher ( -4 < VGL < -16.5).
		 * FIXME: BT could be set to 0b000 for VGH=10, VGL=-7.5. */
		/* CTRL4: GVDD is set to 4.68V. */
		/* CTRL5: Set VCM to 0.8030V. Set VML to 1.104V. */
		/* CTRL1: Set driving capability to "Medium Fast 1". */
		const uint8_t regs[] = {
			MK_ILI9225_REG_PWR_CTRL2, MK_ILI9225_REG_PWR_CTRL3,
			MK_ILI9225_REG_PWR_CTRL4, MK_ILI9225_REG_PWR_CTRL5, 
			MK_ILI9225_REG_PWR_CTRL1
		};
		const uint16_t dat[] = {
			0x0018, 0x6121, 0x006F, 0x495F, 0x0800
		};

		for(uint8_t i = 0; i < sizeof(regs); i++)
			set_register(regs[i], dat[i]);
	}
	ili9225_delay_ms(10);

	/* Enable automatic booster operation, and amplifiers.
	 * Set VCI1 to 2.76V.
	 * FIXME: why is VCI1 changed from 2.58 to 2.76V? */
	set_register(MK_ILI9225_REG_PWR_CTRL2, 0x103B);
	ili9225_delay_ms(50);

	{
		const struct reg_dat_pair cmds[] = {
			/* Set shift direction SS from S528 to S1.
			 * Set active lines NL to 528 * 220 dots. */
			{ MK_ILI9225_REG_DRIVER_OUTPUT_CTRL,	0x011C },
			/* Set LCD inversion to disabled. */
			{ MK_ILI9225_REG_LCD_AC_DRIVING_CTRL,	0x0100 },
			/* Increment vertical and horizontal address.
			 * Use vertical image. */
			{ MK_ILI9225_REG_ENTRY_MODE,		0x1018 },
			/* Turn off all display outputs. */
			{ MK_ILI9225_REG_DISPLAY_CTRL,		0x0000 },
			/* Set porches to 8 lines. */
			{ MK_ILI9225_REG_BLANK_PERIOD_CTRL,		0x0808 },
			/* Use 1-clock delay to gate output and edge. */
			{ MK_ILI9225_REG_FRAME_CYCLE_CTRL,		0x1100 },
			/* Ignore RGB interface settings. */
			{ MK_ILI9225_REG_INTERFACE_CTRL,		0x0000 },
			/* Set oscillation frequency to 266.6 kHz. */
			{ MK_ILI9225_REG_OSC_CTRL,			0x0701 },
			/* Set VCI recycling to 2 clocks. */
			{ MK_ILI9225_REG_VCI_RECYCLING,		0x0020 },
			/* Initialise RAM Address to 0x0 px. */
			{ MK_ILI9225_REG_RAM_ADDR_SET1,		0x0000 },
			{ MK_ILI9225_REG_RAM_ADDR_SET2,		0x0000 },

			/* Set scanning position to full screen. */
			{ MK_ILI9225_REG_GATE_SCAN_CTRL,		0x0000 },
			/* Set end scan position to 219 + 1 px (0xDB). */
			{ MK_ILI9225_REG_VERT_SCROLL_CTRL1,	0x00DB },
			/* Set start scan position to 0 px. */
			{ MK_ILI9225_REG_VERT_SCROLL_CTRL2,	0x0000 },
			/* Set scroll step to 0 px (no scrolling). */
			{ MK_ILI9225_REG_VERT_SCROLL_CTRL3,	0x0000 },
			/* Set partial screen driving end to 219 + 1 px
			 * (0xDB). */
			{ MK_ILI9225_REG_PART_DRIVING_POS1,	0x00DB },
			/* Set partial screen driving start to 0 px. */
			{ MK_ILI9225_REG_PART_DRIVING_POS2,	0x0000 },
			/* Set window to 176 x 220 px (full screen). */
			{ MK_ILI9225_REG_HORI_WIN_ADDR1,		0x00AF },
			{ MK_ILI9225_REG_HORI_WIN_ADDR2,		0x0000 },
			{ MK_ILI9225_REG_VERT_WIN_ADDR1,		0x00DB },
			{ MK_ILI9225_REG_VERT_WIN_ADDR2,		0x0000 },

			/* Gamma curve data. */
			{ MK_ILI9225_REG_GAMMA_CTRL1,		0x0000 },
			{ MK_ILI9225_REG_GAMMA_CTRL2,		0x0808 },
			{ MK_ILI9225_REG_GAMMA_CTRL3,		0x080A },
			{ MK_ILI9225_REG_GAMMA_CTRL4,		0x000A },
			{ MK_ILI9225_REG_GAMMA_CTRL5,		0x0A08 },
			{ MK_ILI9225_REG_GAMMA_CTRL6,		0x0808 },
			{ MK_ILI9225_REG_GAMMA_CTRL7,		0x0000 },
			{ MK_ILI9225_REG_GAMMA_CTRL8,		0x0A00 },
			{ MK_ILI9225_REG_GAMMA_CTRL9,		0x0710 },
			{ MK_ILI9225_REG_GAMMA_CTRL10,		0x0710 },

			/* Enable full colour display. */
			{ MK_ILI9225_REG_DISPLAY_CTRL,		0x0012 }
		};

		for(uint_fast8_t i = 0; i < (uint_fast8_t)ARRAYSIZE(cmds); i++)
			set_register(cmds[i].reg, cmds[i].dat);
	}
	ili9225_delay_ms(50);

	/**
	 * FIXME: TEMON is enabled but FLM isn't exposed?
	 * GON: Enable display.
	 * CL: Use full colour.
	 * REV: reverse greyscale levels.
	 * D: Switch on display.
	 */
	set_register(MK_ILI9225_REG_DISPLAY_CTRL, 0x1017);
	ili9225_delay_ms(50);
	
	/* Turn on backlight */
	ili9225_set_led(1);

#if MK_ILI9225_READ_AVAILABLE
	/* ret should be 0 if the returned ID was 0x9225. */
	ret -= get_register(MK_ILI9225_REG_DRIVER_CODE_READ);
#else
	ret = 0;
#endif

	// DMA Configuration
	{
		// Setup the data channel
		c = dma_channel_get_default_config(dma_tx);  // Default configs
		channel_config_set_transfer_data_size(&c, DMA_SIZE_16);          // 16-bit txfers
		channel_config_set_dreq(&c, spi_get_dreq(ili9225_cfg.spi, true));
	}

	return ret;
}

void ili9225_display_control(bool invert, ili9225_color_mode_e colour_mode)
{
	uint16_t dat = 0x0013;
	dat |= ((uint16_t)invert << 2);
	dat |= ((uint16_t)colour_mode << 3);
	set_register(MK_ILI9225_REG_DISPLAY_CTRL, dat);
}

void ili9225_delay_ms(unsigned ms)
{
	sleep_ms(ms);
}


void ili9225_dma_write(const uint16_t *data, size_t len)
{
	ili9225_write_pixels_start();

    dma_channel_configure(dma_tx, &c,
                          &spi_get_hw(ili9225_cfg.spi)->dr, // write address
                          data, // read address
                          len, // element count (each element is of size transfer_data_size)
                          false); // don't start yet

    dma_start_channel_mask((1u << dma_tx));
}


void ili9225_spi_write16(const uint16_t *halfwords, size_t len)
{
	spi_write16_blocking(ili9225_cfg.spi, halfwords, len);
}

void ili9225_set_led(bool state)
{
	gpio_put(ili9225_cfg.gpio_led, state);
}

/* Functions required for communication with the ILI9225. */
void ili9225_set_rst(bool state)
{
	gpio_put(ili9225_cfg.gpio_rst, state);
}

void ili9225_set_rs(bool state)
{
	gpio_put(ili9225_cfg.gpio_rs, state);
}

void ili9225_set_cs(bool state)
{
	gpio_put(ili9225_cfg.gpio_cs, state);
}

void ili9225_set_window(uint16_t hor_start, uint16_t hor_end,
	uint16_t vert_start, uint16_t vert_end)
{
	assert(hor_start < hor_end);
	assert(hor_end < SCREEN_SIZE_X);
	assert(vert_start < vert_end);
	assert(vert_end < SCREEN_SIZE_Y);

	set_register(MK_ILI9225_REG_HORI_WIN_ADDR1, hor_end);
	set_register(MK_ILI9225_REG_HORI_WIN_ADDR2, hor_start);
	set_register(MK_ILI9225_REG_VERT_WIN_ADDR1, vert_end);
	set_register(MK_ILI9225_REG_VERT_WIN_ADDR2, vert_start);
	set_register(MK_ILI9225_REG_RAM_ADDR_SET1, hor_start);
	set_register(MK_ILI9225_REG_RAM_ADDR_SET2, vert_start);
}

void ili9225_set_x(uint8_t x)
{
	set_register(MK_ILI9225_REG_RAM_ADDR_SET1, x);
}

void ili9225_set_address(uint8_t x, uint8_t y)
{
	set_register(MK_ILI9225_REG_RAM_ADDR_SET1, x);
	set_register(MK_ILI9225_REG_RAM_ADDR_SET2, y);
}

void ili9225_write_pixels(const uint16_t *pixels, uint_fast16_t nmemb)
{
	assert(pixels != NULL);
	assert(nmemb > 0);

	write_register(MK_ILI9225_REG_GRAM_RW);
	ili9225_set_rs(1);
	ili9225_set_cs(0);
	ili9225_spi_write16(pixels, nmemb);
	ili9225_set_cs(1);
}

void ili9225_write_pixels_start(void)
{
	write_register(MK_ILI9225_REG_GRAM_RW);
	ili9225_set_rs(1);
	ili9225_set_cs(0);
}

void ili9225_write_pixels_end(void)
{
	ili9225_set_cs(1);
}

void ili9225_power_control(uint8_t drive_power, bool sleep)
{
	uint16_t data;
	data = (uint16_t)drive_power << 8;
	data |= sleep;
	set_register(MK_ILI9225_REG_PWR_CTRL1, data);
}

void ili9225_set_gate_scan(uint16_t hor_start, uint16_t hor_end)
{
	uint16_t dat = 0x0100;
	uint16_t lcd_line_start = hor_end / 8;
	set_register(MK_ILI9225_REG_DRIVER_OUTPUT_CTRL, dat | lcd_line_start);
	set_register(MK_ILI9225_REG_GATE_SCAN_CTRL, hor_start / 8);
}

void ili9225_set_drive_freq(uint16_t f)
{
	f &= 0x000F;
	f <<= 8;
	f |= 1;
	set_register(MK_ILI9225_REG_OSC_CTRL, f);
}

void ili9225_exit(void)
{
}

void ili9225_fill_rect(uint8_t x,uint8_t y,uint8_t w,uint8_t h,uint16_t color)
{
	set_register(MK_ILI9225_REG_ENTRY_MODE,0x1018);
	set_register(MK_ILI9225_REG_HORI_WIN_ADDR1, y+h-1);			// y_max
	set_register(MK_ILI9225_REG_HORI_WIN_ADDR2, y);				// y_min
	set_register(MK_ILI9225_REG_VERT_WIN_ADDR1, 219-x);			// x_max
	set_register(MK_ILI9225_REG_VERT_WIN_ADDR2, 219-(x+w-1));	// x_min
	set_register(MK_ILI9225_REG_RAM_ADDR_SET1,y);
	set_register(MK_ILI9225_REG_RAM_ADDR_SET2,219-x);
	write_register(MK_ILI9225_REG_GRAM_RW);
	ili9225_set_rs(1);
	ili9225_set_cs(0);
	for(uint16_t i=0;i<w*h;i++) {
		ili9225_spi_write16(&color,1);
	}
	ili9225_set_cs(1);
}

void ili9225_fill(uint16_t color)
{
	ili9225_fill_rect(0,0,220,176,color);
}

void ili9225_pixel(uint8_t x,uint8_t y,uint16_t color)
{
	set_register(MK_ILI9225_REG_RAM_ADDR_SET1,y);
	set_register(MK_ILI9225_REG_RAM_ADDR_SET2,219-x);
	set_register(MK_ILI9225_REG_GRAM_RW,color);
}

void ili9225_blit(uint16_t *fbuf,uint8_t x,uint8_t y,uint8_t w,uint8_t h) {
	set_register(MK_ILI9225_REG_ENTRY_MODE,0x1018);
	set_register(MK_ILI9225_REG_HORI_WIN_ADDR1, y+h-1);			// y_max
	set_register(MK_ILI9225_REG_HORI_WIN_ADDR2, y);				// y_min
	set_register(MK_ILI9225_REG_VERT_WIN_ADDR1, 219-x);			// x_max
	set_register(MK_ILI9225_REG_VERT_WIN_ADDR2, 219-(x+w-1));	// x_min
	set_register(MK_ILI9225_REG_RAM_ADDR_SET1,y);
	set_register(MK_ILI9225_REG_RAM_ADDR_SET2,219-x);
	write_register(MK_ILI9225_REG_GRAM_RW);
	ili9225_set_rs(1);
	ili9225_set_cs(0);
	ili9225_spi_write16(fbuf,w*h);
	ili9225_set_cs(1);
}

void ili9225_get_letter(uint16_t *fbuf,char l,uint16_t color,uint16_t bgcolor) {
	uint8_t letter[8];
	uint8_t row;
	
	switch(l)
	{
		case 'a':
		case 'A':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
						              0b01111110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case 'b':
		case 'B':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'c':
		case 'C':
		{
			const uint8_t letter_[8]={0b00011110,
						              0b00110000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00110000,
						              0b00011110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'd':
		case 'D':
		{
			const uint8_t letter_[8]={0b01111000,
						              0b01101100,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01101100,
						              0b01111000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'e':
		case 'E':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01100000,
						              0b01111000,
						              0b01100000,
						              0b01100000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'f':
		case 'F':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01100000,
						              0b01111000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'g':
		case 'G':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100000,
						              0b01101110,
						              0b01100110,
						              0b01100110,
						              0b00111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'h':
		case 'H':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01111110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'i':
		case 'I':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'j':
		case 'J':
		{
			const uint8_t letter_[8]={0b00000110,
						              0b00000110,
						              0b00000110,
						              0b00000110,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'k':
		case 'K':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11001100,
						              0b11011000,
						              0b11110000,
						              0b11011000,
						              0b11001100,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'l':
		case 'L':
		{
			const uint8_t letter_[8]={0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'm':
		case 'M':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11101110,
						              0b11111110,
						              0b11010110,
						              0b11000110,
						              0b11000110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'n':
		case 'N':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11100110,
						              0b11110110,
						              0b11011110,
						              0b11001110,
						              0b11000110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'o':
		case 'O':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'p':
		case 'P':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01100000,
						              0b01100000,
						              0b01100000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'q':
		case 'Q':
		{
			const uint8_t letter_[8]={0b01111000,
						              0b11001100,
						              0b11001100,
						              0b11001100,
						              0b11001100,
						              0b11011100,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'r':
		case 'R':
		{
			const uint8_t letter_[8]={0b01111100,
						              0b01100110,
						              0b01100110,
						              0b01111100,
						              0b01101100,
						              0b01100110,
						              0b01100110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 's':
		case 'S':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01110000,
						              0b00111100,
						              0b00001110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 't':
		case 'T':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'u':
		case 'U':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'v':
		case 'V':
		{
			const uint8_t letter_[8]={0b01100110,
						              0b01100110,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00111100,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'w':
		case 'W':
		{
			const uint8_t letter_[8]={0b11000110,
						              0b11000110,
						              0b11000110,
						              0b11010110,
						              0b11111110,
						              0b11101110,
						              0b11000110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'x':
		case 'X':
		{
			const uint8_t letter_[8]={0b11000011,
						              0b01100110,
						              0b00111100,
						              0b00011000,
						              0b00111100,
						              0b01100110,
						              0b11000011,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'y':
		case 'Y':
		{
			const uint8_t letter_[8]={0b11000011,
						              0b01100110,
						              0b00111100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case 'z':
		case 'Z':
		{
			const uint8_t letter_[8]={0b11111110,
						              0b00001100,
						              0b00011000,
						              0b00110000,
						              0b01100000,
						              0b11000000,
						              0b11111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case '-':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b01111110,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case '(':
		case '[':
		case '{':
		{
			const uint8_t letter_[8]={0b00001100,
						              0b00011000,
						              0b00110000,
									  0b00110000,
						              0b00110000,
						              0b00011000,
						              0b00001100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

        case ')':
		case ']':
		case '}':
		{
			const uint8_t letter_[8]={0b00110000,
						              0b00011000,
						              0b00001100,
									  0b00001100,
						              0b00001100,
						              0b00011000,
						              0b00110000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case ',':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b00000000,
						              0b00000000,
						              0b00011000,
						              0b00011000,
						              0b00110000};
			memcpy(letter,letter_,8);
			break;
		}

		case '.':
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
									  0b00000000,
						              0b00000000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '!':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00011000,
						              0b00011000,
									  0b00011000,
						              0b00011000,
						              0b00000000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '&':
		{
			const uint8_t letter_[8]={0b00111000,
						              0b01101100,
						              0b01101000,
									  0b01110110,
						              0b11011100,
						              0b11001110,
						              0b01111011,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '\'':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00011000,
						              0b00110000,
									  0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '0':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01101110,
									  0b01111110,
						              0b01110110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '1':
		{
			const uint8_t letter_[8]={0b00011000,
						              0b00111000,
						              0b01111000,
									  0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '2':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b00000110,
									  0b00001100,
						              0b00011000,
						              0b00110000,
						              0b01111110,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '3':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b00000110,
									  0b00011100,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '4':
		{
			const uint8_t letter_[8]={0b00011100,
						              0b00111100,
						              0b01101100,
									  0b11001100,
						              0b11111110,
						              0b00001100,
						              0b00001100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '5':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b01100000,
						              0b01111100,
									  0b00000110,
						              0b00000110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '6':
		{
			const uint8_t letter_[8]={0b00011100,
						              0b00110000,
						              0b01100000,
									  0b01111100,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '7':
		{
			const uint8_t letter_[8]={0b01111110,
						              0b00000110,
						              0b00000110,
									  0b00001100,
						              0b00011000,
						              0b00011000,
						              0b00011000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '8':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
									  0b00111100,
						              0b01100110,
						              0b01100110,
						              0b00111100,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		case '9':
		{
			const uint8_t letter_[8]={0b00111100,
						              0b01100110,
						              0b01100110,
									  0b00111110,
						              0b00000110,
						              0b00001100,
						              0b00111000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}

		default:
		{
			const uint8_t letter_[8]={0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000,
						              0b00000000};
			memcpy(letter,letter_,8);
			break;
		}
	}

	for(uint8_t y=0;y<8;y++) {
		row=letter[y];
		for(uint8_t x=0;x<8;x++) {
			if(row & 128) {
				fbuf[y*8+x]=color;
			} else {
				fbuf[y*8+x]=bgcolor;
			}
			row=row<<1;
		}
	}
}

void ili9225_text(char *s,uint8_t x,uint8_t y,uint16_t color,uint16_t bgcolor) {
	uint16_t fbuf[8*8];
	for(uint8_t i=0;i<strlen(s);i++) {
		ili9225_get_letter(fbuf,s[i],color,bgcolor);
		ili9225_blit(fbuf,x,y,8,8);
		x+=8;
		if(x>27*8) {
			break;
		}
	}
}