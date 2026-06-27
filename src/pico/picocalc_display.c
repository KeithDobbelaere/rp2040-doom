#include "picocalc_display.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

#define PICOCALC_LCD_SPI      spi1
#define PICOCALC_LCD_BAUD     75000000u

#define PICOCALC_PIN_SCK      10
#define PICOCALC_PIN_MOSI     11
#define PICOCALC_PIN_CS       13
#define PICOCALC_PIN_DC       14
#define PICOCALC_PIN_RST      15

#define ILI9488_CMD_SWRESET   0x01
#define ILI9488_CMD_SLPOUT    0x11
#define ILI9488_CMD_NORON     0x13
#define ILI9488_CMD_INVON     0x21
#define ILI9488_CMD_DISPON    0x29
#define ILI9488_CMD_CASET     0x2A
#define ILI9488_CMD_PASET     0x2B
#define ILI9488_CMD_RAMWR     0x2C
#define ILI9488_CMD_MADCTL    0x36
#define ILI9488_CMD_COLMOD    0x3A

static bool s_initialized;
static bool s_frame_streaming;
static int s_dma_chan = -1;
static dma_channel_config s_dma_cfg;
static bool s_dma_active;
static int s_dma_active_slab = -1;

static unsigned s_fill_slab;
static unsigned s_fill_row;
static unsigned s_fill_rows_in_slab;

static uint16_t s_dma_slabs[2][PICOCALC_DMA_SLAB_ROWS][PICOCALC_LCD_W]
        __attribute__((aligned(4)));

static uint16_t s_fallback_row[PICOCALC_LCD_W] __attribute__((aligned(4)));

static void lcd_wait_idle(void)
{
    while (spi_get_hw(PICOCALC_LCD_SPI)->sr & SPI_SSPSR_BSY_BITS) {
        tight_loop_contents();
    }
}

static void lcd_wait_dma_done(void)
{
    if (s_dma_active && s_dma_chan >= 0) {
        dma_channel_wait_for_finish_blocking(s_dma_chan);
        s_dma_active = false;
        s_dma_active_slab = -1;
    }
}

static void lcd_wait_dma_and_spi_idle(void)
{
    lcd_wait_dma_done();
    lcd_wait_idle();
}

static void lcd_start_dma_words(const uint16_t *src, uint32_t word_count)
{
    if (s_dma_chan < 0 || !word_count) {
        return;
    }

    dma_channel_configure(
        s_dma_chan,
        &s_dma_cfg,
        &spi_get_hw(PICOCALC_LCD_SPI)->dr,
        src,
        word_count,
        true
    );

    s_dma_active = true;
}

static void lcd_start_dma_slab(unsigned slab, unsigned rows)
{
    if (rows == 0 || rows > PICOCALC_DMA_SLAB_ROWS) {
        return;
    }

    lcd_start_dma_words(&s_dma_slabs[slab][0][0],
                        rows * PICOCALC_LCD_W);

    s_dma_active_slab = (int)slab;
}

static void lcd_select(void)
{
    gpio_put(PICOCALC_PIN_CS, 0);
}

static void lcd_deselect(void)
{
    gpio_put(PICOCALC_PIN_CS, 1);
}

static void lcd_cmd(uint8_t cmd)
{
    lcd_select();
    gpio_put(PICOCALC_PIN_DC, 0);
    spi_write_blocking(PICOCALC_LCD_SPI, &cmd, 1);
    lcd_deselect();
}

static void lcd_data(const uint8_t *data, size_t len)
{
    if (!len) {
        return;
    }

    lcd_select();
    gpio_put(PICOCALC_PIN_DC, 1);
    spi_write_blocking(PICOCALC_LCD_SPI, data, len);
    lcd_deselect();
}

static void lcd_cmd1(uint8_t cmd, uint8_t arg)
{
    lcd_cmd(cmd);
    lcd_data(&arg, 1);
}

static void lcd_set_window(int x0, int y0, int x1, int y1)
{
    uint8_t data[4];

    lcd_cmd(ILI9488_CMD_CASET);
    data[0] = (uint8_t)(x0 >> 8);
    data[1] = (uint8_t)(x0);
    data[2] = (uint8_t)(x1 >> 8);
    data[3] = (uint8_t)(x1);
    lcd_data(data, sizeof(data));

    lcd_cmd(ILI9488_CMD_PASET);
    data[0] = (uint8_t)(y0 >> 8);
    data[1] = (uint8_t)(y0);
    data[2] = (uint8_t)(y1 >> 8);
    data[3] = (uint8_t)(y1);
    lcd_data(data, sizeof(data));

    lcd_cmd(ILI9488_CMD_RAMWR);
}

static void lcd_write_rgb565_row(int y, const uint16_t *row565)
{
    static uint8_t tx[PICOCALC_LCD_W * 2];

    for (int x = 0; x < PICOCALC_LCD_W; x++) {
        uint16_t c = row565 ? row565[x] : 0;
        tx[x * 2 + 0] = (uint8_t)(c >> 8);
        tx[x * 2 + 1] = (uint8_t)(c);
    }

    lcd_set_window(0, y, PICOCALC_LCD_W - 1, y);
    lcd_data(tx, sizeof(tx));
}

static void lcd_clear_black(void)
{
    for (int y = 0; y < PICOCALC_LCD_H; y++) {
        lcd_write_rgb565_row(y, NULL);
    }
}

void picocalc_display_init(void)
{
    if (s_initialized) {
        return;
    }

    uint actual_baud = spi_init(PICOCALC_LCD_SPI, PICOCALC_LCD_BAUD);

    printf("picocalc: spi requested=%lu actual=%lu\r\n",
        (unsigned long)PICOCALC_LCD_BAUD,
        (unsigned long)actual_baud);
    spi_set_format(PICOCALC_LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    if (s_dma_chan < 0) {
        s_dma_chan = dma_claim_unused_channel(true);
        s_dma_cfg = dma_channel_get_default_config(s_dma_chan);

        channel_config_set_transfer_data_size(&s_dma_cfg, DMA_SIZE_16);
        channel_config_set_read_increment(&s_dma_cfg, true);
        channel_config_set_write_increment(&s_dma_cfg, false);
        channel_config_set_dreq(&s_dma_cfg, spi_get_dreq(PICOCALC_LCD_SPI, true));
    }

    gpio_set_function(PICOCALC_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PICOCALC_PIN_MOSI, GPIO_FUNC_SPI);

    gpio_init(PICOCALC_PIN_CS);
    gpio_set_dir(PICOCALC_PIN_CS, GPIO_OUT);
    gpio_put(PICOCALC_PIN_CS, 1);

    gpio_init(PICOCALC_PIN_DC);
    gpio_set_dir(PICOCALC_PIN_DC, GPIO_OUT);
    gpio_put(PICOCALC_PIN_DC, 1);

    gpio_init(PICOCALC_PIN_RST);
    gpio_set_dir(PICOCALC_PIN_RST, GPIO_OUT);

    gpio_put(PICOCALC_PIN_RST, 0);
    sleep_ms(50);
    gpio_put(PICOCALC_PIN_RST, 1);
    sleep_ms(120);

    lcd_cmd(ILI9488_CMD_SWRESET);
    sleep_ms(150);

    lcd_cmd(ILI9488_CMD_SLPOUT);
    sleep_ms(120);

    // PicoCalc ILI9488 needs display inversion enabled.
    // OpenLara does this; without it, black may show as white.
    lcd_cmd(ILI9488_CMD_INVON);

    // RGB565.
    lcd_cmd1(ILI9488_CMD_COLMOD, 0x55);

    // Memory access control.
    // If the image is rotated/mirrored later, this is the first byte to change.
    lcd_cmd1(ILI9488_CMD_MADCTL, 0x48);

    lcd_cmd(ILI9488_CMD_NORON);
    sleep_ms(20);

    lcd_cmd(ILI9488_CMD_DISPON);
    sleep_ms(120);

    lcd_clear_black();

    s_initialized = true;
}

void picocalc_display_begin_frame(void)
{
    if (!s_initialized) {
        return;
    }

    lcd_wait_dma_and_spi_idle();

    // Open one continuous 320x200 RAMWR window centered on the 320x320 LCD.
    lcd_set_window(0,
                   PICOCALC_DOOM_Y,
                   PICOCALC_LCD_W - 1,
                   PICOCALC_DOOM_Y + PICOCALC_DOOM_H - 1);

    gpio_put(PICOCALC_PIN_DC, 1);
    gpio_put(PICOCALC_PIN_CS, 0);

    spi_set_format(PICOCALC_LCD_SPI, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    s_frame_streaming = true;

    s_dma_active = false;
    s_dma_active_slab = -1;

    s_fill_slab = 0;
    s_fill_row = 0;
    s_fill_rows_in_slab = 0;
}

uint16_t *picocalc_display_acquire_row_buffer(int doom_y)
{
    if (!s_initialized) {
        return NULL;
    }

    if (doom_y < 0 || doom_y >= PICOCALC_DOOM_H) {
        return NULL;
    }

    if (s_frame_streaming && s_dma_chan >= 0) {
        // Protect against accidental reuse. This should usually not wait,
        // because we build slab A while slab B is being transmitted.
        if (s_dma_active && s_dma_active_slab == (int)s_fill_slab) {
            lcd_wait_dma_done();
        }

        return s_dma_slabs[s_fill_slab][s_fill_row];
    }

    return s_fallback_row;
}

void picocalc_display_submit_row_buffer(int doom_y, uint16_t *row565)
{
    if (!s_initialized || !row565) {
        return;
    }

    if (doom_y < 0 || doom_y >= PICOCALC_DOOM_H) {
        return;
    }

    if (s_frame_streaming && s_dma_chan >= 0) {
        uint16_t *expected = s_dma_slabs[s_fill_slab][s_fill_row];

        if (row565 != expected) {
            memcpy(expected, row565, PICOCALC_LCD_W * sizeof(uint16_t));
        }

        s_fill_row++;
        s_fill_rows_in_slab++;

        const bool slab_full = s_fill_row >= PICOCALC_DMA_SLAB_ROWS;
        const bool frame_last_row = doom_y == (PICOCALC_DOOM_H - 1);

        if (slab_full || frame_last_row) {
            const unsigned slab_to_send = s_fill_slab;
            const unsigned rows_to_send = s_fill_rows_in_slab;

            // Previous slab DMA ran while we filled this slab.
            // Wait only when we need the DMA channel for the next slab.
            lcd_wait_dma_done();

            lcd_start_dma_slab(slab_to_send, rows_to_send);

            s_fill_slab ^= 1u;
            s_fill_row = 0;
            s_fill_rows_in_slab = 0;
        }

    } else if (s_frame_streaming) {
        spi_write16_blocking(PICOCALC_LCD_SPI, row565, PICOCALC_LCD_W);
    } else {
        lcd_write_rgb565_row(PICOCALC_DOOM_Y + doom_y, row565);
    }
}

void picocalc_display_write_doom_row(int doom_y, const uint16_t *row565)
{
    if (!row565) {
        return;
    }

    uint16_t *dst = picocalc_display_acquire_row_buffer(doom_y);

    if (!dst) {
        return;
    }

    if (dst != row565) {
        memcpy(dst, row565, PICOCALC_LCD_W * sizeof(uint16_t));
    }

    picocalc_display_submit_row_buffer(doom_y, dst);
}

void picocalc_display_end_frame(void)
{
    if (!s_initialized || !s_frame_streaming) {
        return;
    }

    lcd_wait_dma_and_spi_idle();

    spi_set_format(PICOCALC_LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_put(PICOCALC_PIN_CS, 1);

    s_frame_streaming = false;
}