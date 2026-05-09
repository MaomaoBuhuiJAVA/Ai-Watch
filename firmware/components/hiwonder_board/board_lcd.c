#include "board_lcd.h"
#include "xl9555.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#define LCD_HOST    SPI2_HOST
#define LCD_MOSI    47
#define LCD_SCLK    21
#define LCD_CS      2
#define LCD_DC      3
#define LCD_RST     (-1)

#define LCD_BL(x)   do { xl9555_pin_write(BACKLIGHT_IO, (x) ? 1 : 0); } while (0)

_lcd_dev lcddev;
uint32_t g_back_color = BLACK;

static const char *TAG = "board_lcd";

void lcd_init(void)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;

    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_CS,
        .dc_gpio_num = LCD_DC,
        .pclk_hz = 60 * 1000 * 1000,
        .spi_mode = 0,
        .trans_queue_depth = 7,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &lcddev.lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcddev.lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcddev.lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(lcddev.lcd_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcddev.lcd_panel_handle, true));

    lcddev.width = 320;
    lcddev.height = 240;
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(lcddev.lcd_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(lcddev.lcd_panel_handle, true, false));

    LCD_BL(1);
    lcd_clear(BLACK);
    ESP_LOGI(TAG, "LCD init ok");
}

void lcd_clear(uint16_t color)
{
    size_t line_size = lcddev.width * sizeof(uint16_t);
    uint16_t *line_buffer = (uint16_t *)heap_caps_malloc(line_size, MALLOC_CAP_DMA);
    if (!line_buffer) {
        ESP_LOGE(TAG, "lcd_clear malloc failed");
        return;
    }
    for (int i = 0; i < (int)lcddev.width; i++) {
        line_buffer[i] = color;
    }
    for (int y = 0; y < (int)lcddev.height; y++) {
        esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, 0, y, lcddev.width, y + 1, line_buffer);
    }
    heap_caps_free(line_buffer);
}
