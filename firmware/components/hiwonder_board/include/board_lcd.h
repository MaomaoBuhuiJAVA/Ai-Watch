#ifndef BOARD_LCD_H
#define BOARD_LCD_H

#include "esp_lcd_panel_ops.h"
#include <stdint.h>

#define WHITE           0xFFFF
#define BLACK           0x0000
#define RED             0xF800
#define GREEN           0x07E0
#define BLUE            0x001F
#define MAGENTA         0xF81F
#define YELLOW          0xFFE0
#define CYAN            0x07FF

typedef struct {
    uint16_t id;
    uint32_t width;
    uint32_t height;
    uint8_t dir;
    uint8_t color_byte;
    esp_lcd_panel_handle_t lcd_panel_handle;
    struct {
        int lcd_rst;
        int lcd_bl;
    } ctrl;
} _lcd_dev;

extern _lcd_dev lcddev;
extern uint32_t g_back_color;

void lcd_init(void);
void lcd_clear(uint16_t color);

#endif
