#include "picocalc_display.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "hardware/gpio.h"
#include "hardware/spi.h"
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

    spi_init(PICOCALC_LCD_SPI, PICOCALC_LCD_BAUD);
    spi_set_format(PICOCALC_LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

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
}

void picocalc_display_write_doom_row(int doom_y, const uint16_t *row565)
{
    if (!s_initialized) {
        return;
    }

    if (doom_y < 0 || doom_y >= PICOCALC_DOOM_H) {
        return;
    }

    lcd_write_rgb565_row(PICOCALC_DOOM_Y + doom_y, row565);
}

void picocalc_display_end_frame(void)
{
}