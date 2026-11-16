#include "display.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#include "font5x7.h"

#define DISPLAY_SDA_GPIO    GPIO_NUM_21
#define DISPLAY_SCL_GPIO    GPIO_NUM_22
#define DISPLAY_I2C_ADDRESS 0x3C
#define DISPLAY_I2C_HZ      400000
#define DISPLAY_TIMEOUT_MS  100

#define DISPLAY_WIDTH       128
#define DISPLAY_PAGES       8
#define DISPLAY_CHAR_WIDTH  (FONT5X7_WIDTH + 1)
#define DISPLAY_COLUMNS     (DISPLAY_WIDTH / DISPLAY_CHAR_WIDTH)

#define SSD1306_CONTROL_COMMAND 0x00
#define SSD1306_CONTROL_DATA    0x40

static const char *TAG = "DISPLAY";

static i2c_master_dev_handle_t device = NULL; //ako je NULL, onda nije inicijalizovan

static uint8_t frame[1 + DISPLAY_WIDTH * DISPLAY_PAGES] = { //prvi bajt je kontrolni, ostatak su pikseli
    SSD1306_CONTROL_DATA
};

static uint8_t *const pixels = &frame[1];

static const uint8_t init_commands[] = {
    SSD1306_CONTROL_COMMAND,
    0xAE,       /* display off */
    0xD5, 0x80, /* clock divide */
    0xA8, 0x3F, /* multiplex: 64 rows */
    0xD3, 0x00, /* no vertical offset */
    0x40,       /* start line 0 */
    0x8D, 0x14, /* enable charge pump */
    0x20, 0x00, /* horizontal addressing */
    0xA1,       /* mirror columns */
    0xC8,       /* mirror rows */
    0xDA, 0x12, /* COM pin layout */
    0x81, 0xCF, /* contrast */
    0xD9, 0xF1, /* pre-charge */
    0xDB, 0x40, /* VCOM level */
    0xA4,       /* show RAM contents */
    0xA6,       /* normal, not inverted */
    0xAF        /* display on */
};

static const uint8_t address_commands[] = { //pocinje pisanje od 0,0 do 127,7
    SSD1306_CONTROL_COMMAND,
    0x21, 0, DISPLAY_WIDTH - 1,
    0x22, 0, DISPLAY_PAGES - 1
};

static esp_err_t transmit(const uint8_t *data, size_t length) //slanje podataka na oled
{
    return i2c_master_transmit(device, data, length, DISPLAY_TIMEOUT_MS);
}

static void draw_text(int page, const char *text, bool centered)
{
    size_t length = strnlen(text, DISPLAY_COLUMNS); 
    int x = centered //ako treba centrirati,inace od leve ivice
        ? (DISPLAY_WIDTH - (int)length * DISPLAY_CHAR_WIDTH) / 2
        : 0;

    for (size_t i = 0; i < length; i++) { //karakter po karakter, ako nije prepoznat ? pisemo
        char c = text[i];

        if (c < FONT5X7_FIRST_CHAR || c > FONT5X7_LAST_CHAR) {
            c = '?';
        }

        memcpy(&pixels[page * DISPLAY_WIDTH + x], //ispisivanje karaktera u piksele
               font5x7[c - FONT5X7_FIRST_CHAR],
               FONT5X7_WIDTH);

        x += DISPLAY_CHAR_WIDTH;
    }
}

esp_err_t display_init(void)
{
    i2c_master_bus_config_t bus_config = { //podesavanje magistrale
        .i2c_port = I2C_NUM_0,
        .sda_io_num = DISPLAY_SDA_GPIO,
        .scl_io_num = DISPLAY_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true
    };

    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Greska pri inicijalizaciji I2C magistrale: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = i2c_master_probe(bus, DISPLAY_I2C_ADDRESS, DISPLAY_TIMEOUT_MS);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OLED nije pronadjen na adresi 0x%02X: %s",
                 DISPLAY_I2C_ADDRESS, esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t device_config = { 
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DISPLAY_I2C_ADDRESS,
        .scl_speed_hz = DISPLAY_I2C_HZ
    };

    err = i2c_master_bus_add_device(bus, &device_config, &device);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Greska pri dodavanju OLED uredjaja: %s",
                 esp_err_to_name(err));
        return err;
    }

    return transmit(init_commands, sizeof(init_commands)); //prenos inicijalizacionih komandi na oled
}

esp_err_t display_show(const display_view_t *view) //crtanje kadra
{
    if (view == NULL) { //nema podataka
        return ESP_ERR_INVALID_ARG;
    }

    if (device == NULL) { //nije inicijalizovan
        return ESP_ERR_INVALID_STATE;
    }

    char line[32];

    memset(pixels, 0, DISPLAY_WIDTH * DISPLAY_PAGES);

    if (view->dht_valid) { //ako su podaci validni, ispisujemo ih
        snprintf(line, sizeof(line), "Temp: %.1f C",
                 (double)view->temperature);
        draw_text(0, line, false);
        snprintf(line, sizeof(line), "Vlaznost: %.1f %%",
                 (double)view->humidity);
        draw_text(1, line, false);
    } else {
        draw_text(0, "Temp: --", false);
        draw_text(1, "Vlaznost: --", false);
    }

    snprintf(line, sizeof(line), "CO: %d mV", view->co_mv);  //ispisi co vrednosti
    draw_text(2, line, false);

    draw_text(5, view->status, true);

    esp_err_t err = transmit(address_commands, sizeof(address_commands));

    if (err != ESP_OK) {
        return err;
    }

    return transmit(frame, sizeof(frame));
}
