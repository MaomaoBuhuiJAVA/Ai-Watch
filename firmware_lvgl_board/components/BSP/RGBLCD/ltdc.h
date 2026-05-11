#ifndef _LTDC_H
#define _LTDC_H

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"

#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "xl9555.h"
#include <math.h>
#include <string.h>
#include <stdint.h>

/* 颜色（RGB565） */
#define WHITE           0xFFFF
#define BLACK           0x0000
#define RED             0xF800
#define GREEN           0x07E0
#define BLUE            0x001F
#define MAGENTA         0xF81F
#define YELLOW          0xFFE0
#define CYAN            0x07FF

/* 不常用颜色 */
#define BROWN           0xBC40
#define BRRED           0xFC07
#define GRAY            0x8430
#define DARKBLUE        0x01CF
#define LIGHTBLUE       0x7D7C
#define GRAYBLUE        0x5458
#define LIGHTGREEN      0x841F
#define LGRAY           0xC618
#define LGRAYBLUE       0xA651
#define LBBLUE          0x2B12

/* LCD 重要参数结构体 */
typedef struct
{
    uint16_t id;                                /* SPI 屏通常不通过这个读取，保留以防 */
    uint32_t width;                             /* 面板宽度（固定） */
    uint32_t height;                            /* 面板高度（固定） */
    uint8_t  dir;                               /* 方向 0:竖屏, 1:横屏 */
    uint8_t  color_byte;                        /* 单像素字节数（RGB565 -> 2） */
    esp_lcd_panel_handle_t lcd_panel_handle;    /* LCD 控制句柄 */
    struct
    {
        int lcd_rst;                            /* 复位引脚（-1 表示无硬件复位） */
        int lcd_bl;                             /* 背光引脚（若用 GPIO，否则用 XL9555） */
    } ctrl;
} _lcd_dev;

/* 全局变量 */
extern _lcd_dev lcddev;
extern uint32_t g_back_color;

/* 函数声明 */
void lcd_init(void);
void lcd_clear(uint16_t color);
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color);
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color);
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color);
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color);
void lcd_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color);
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color);
void lcd_fill_circle(uint16_t center_x, uint16_t center_y, uint16_t radius, uint16_t color);
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color);
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color);
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color);
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color);

#endif
