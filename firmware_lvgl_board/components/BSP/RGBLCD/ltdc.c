#include "ltdc.h"
#include "ltdcfont.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/spi_master.h"
#include "xl9555.h"
#include "esp_heap_caps.h"
#define LCD_HOST           SPI2_HOST
#define LCD_MOSI           47
#define LCD_SCLK           21
#define LCD_CS             2
#define LCD_DC             3
#define LCD_RST            -1   /* -1 表示不使用硬件复位 */
#define LCD_BL_IO          BACKLIGHT_IO 

/* 局部 LOG 标签 */
static const char *TAG = "lcd_st7789";

/* 全局变量定义 */
_lcd_dev lcddev;
uint32_t g_back_color = WHITE;

/* 背光控制（通过 XL9555 的 P16） */
#define LCD_BL(x)   do { xl9555_pin_write(LCD_BL_IO, (x) ? 1 : 0); } while(0)

/* --------------------- lcd 初始化 --------------------- */
void lcd_init(void)
{
    esp_err_t ret;
        /* SPI 总线配置 */
    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_MOSI,
        .sclk_io_num = LCD_SCLK,
        .max_transfer_sz = 320 * 240 * 2 + 8, 
        .miso_io_num = -1, .quadwp_io_num = -1, .quadhd_io_num = -1,
    };
    // ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    /* SPI 面板 IO 配置 */
    esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num = LCD_CS, .dc_gpio_num = LCD_DC,
        .pclk_hz = 60 * 1000 * 1000, 
        .spi_mode = 0,
        .trans_queue_depth = 7, // 使用 7
        .lcd_cmd_bits = 8, .lcd_param_bits = 8,
    };
    esp_lcd_panel_io_handle_t io_handle; 
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));


    /* ST7789 面板配置 */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_RST,
        .rgb_ele_order = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = 16,
        .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE, 
    };
    // 【注意】: panel_handle 现在是全局的 lcddev.lcd_panel_handle
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &lcddev.lcd_panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcddev.lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcddev.lcd_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(lcddev.lcd_panel_handle, true)); // 【关键】: 开启反色
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcddev.lcd_panel_handle, true)); // 【关键】: 确保显示开启


    lcddev.width = 320;
    lcddev.height = 240;
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(lcddev.lcd_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(lcddev.lcd_panel_handle, true, false));

    // 清屏和打开背光
    LCD_BL(1);
    lcd_clear(BLACK); 
    ESP_LOGI(TAG, "LCD initialized with correct parameters.");
}

/* --------------------- 基本绘图接口 --------------------- */

/* 清屏（一次性分配整屏缓存并发送） */
// 在 ltdc.c 中

/* 清屏（优化版：分行清屏） */
void lcd_clear(uint16_t color)
{
    // 计算一行像素所需的缓冲区大小
    size_t line_size = lcddev.width * sizeof(uint16_t);
    
    // 申请一个能够容纳一行的DMA缓冲区
    uint16_t *line_buffer = (uint16_t *)heap_caps_malloc(line_size, MALLOC_CAP_DMA);
    if (line_buffer == NULL) {
        ESP_LOGE(TAG, "lcd_clear: line_buffer malloc failed");
        return;
    }

    // 将这一行缓冲区填满指定的颜色
    for (int i = 0; i < lcddev.width; i++) {
        line_buffer[i] = color;
    }

    // 循环发送这一行数据，直到填满整个屏幕
    for (int y = 0; y < lcddev.height; y++) {
        esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, 0, y, lcddev.width, y + 1, line_buffer);
    }
    
    // 释放缓冲区
    heap_caps_free(line_buffer);
}


/* 画点（单像素） */
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= lcddev.width || y >= lcddev.height) return;
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, x, y, x + 1, y + 1, &color));
}

/* 填充单色区域：包含边界检查 */
void lcd_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t color)
{
    if (sx >= lcddev.width || sy >= lcddev.height || ex >= lcddev.width || ey >= lcddev.height) {
        ESP_LOGE(TAG, "lcd_fill: out of range");
        return;
    }
    if (ex < sx || ey < sy) {
        ESP_LOGE(TAG, "lcd_fill: invalid coords");
        return;
    }

    uint16_t width = ex - sx + 1;
    uint16_t height = ey - sy + 1;

    size_t line_size = width * sizeof(uint16_t);
    uint16_t *line = heap_caps_malloc(line_size, MALLOC_CAP_INTERNAL);
    if (!line) {
        ESP_LOGE(TAG, "lcd_fill: line malloc failed");
        return;
    }
    for (uint16_t i = 0; i < width; i++) line[i] = color;

    for (uint16_t row = 0; row < height; row++) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle,
                                                 sx, sy + row, sx + width - 1, sy + row + 1, line));
    }
    heap_caps_free(line);
}

/* 按颜色数组填充（按行发送） */
void lcd_color_fill(uint16_t sx, uint16_t sy, uint16_t ex, uint16_t ey, uint16_t *color)
{
    if (sx >= lcddev.width || sy >= lcddev.height || ex >= lcddev.width || ey >= lcddev.height) {
        ESP_LOGE(TAG, "lcd_color_fill: out of range");
        return;
    }
    if (ex < sx || ey < sy) {
        ESP_LOGE(TAG, "lcd_color_fill: invalid coords");
        return;
    }

    uint16_t width = ex - sx + 1;
    uint16_t height = ey - sy + 1;
    uint32_t idx = 0;

    uint16_t *line = heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    if (!line) {
        ESP_LOGE(TAG, "lcd_color_fill: line malloc failed");
        return;
    }

    for (uint16_t row = 0; row < height; row++) {
        for (uint16_t col = 0; col < width; col++) {
            line[col] = color[idx++];
        }
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle,
                                                 sx, sy + row, sx + width - 1, sy + row + 1, line));
    }
    heap_caps_free(line);
}

/* --------------------- 图形算法（保留原逻辑并作适配） --------------------- */

/* 画线（Bresenham） */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color)
{
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, row, col;
    delta_x = (int)x2 - (int)x1;
    delta_y = (int)y2 - (int)y1;
    row = x1;
    col = y1;

    if (delta_x > 0) incx = 1;
    else if (delta_x == 0) incx = 0;
    else { incx = -1; delta_x = -delta_x; }

    if (delta_y > 0) incy = 1;
    else if (delta_y == 0) incy = 0;
    else { incy = -1; delta_y = -delta_y; }

    distance = (delta_x > delta_y) ? delta_x : delta_y;

    for (int t = 0; t <= distance + 1; t++) {
        lcd_draw_point(row, col, color);
        xerr += delta_x;
        yerr += delta_y;
        if (xerr > distance) { xerr -= distance; row += incx; }
        if (yerr > distance) { yerr -= distance; col += incy; }
    }
}

/* 画水平线（优化：一次发送整行） */
void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t len, uint16_t color)
{
    if (len == 0) return;
    if (x >= lcddev.width || y >= lcddev.height) return;
    uint16_t ex = x + len - 1;
    if (ex >= lcddev.width) ex = lcddev.width - 1;
    uint16_t width = ex - x + 1;

    size_t buf_size = width * sizeof(uint16_t);
    uint16_t *buf = heap_caps_malloc(buf_size, MALLOC_CAP_INTERNAL);
    if (!buf) return;
    for (uint32_t i = 0; i < width; i++) buf[i] = color;

    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(lcddev.lcd_panel_handle, x, y, x + width - 1, y + 1, buf));
    heap_caps_free(buf);
}

/* 画矩形（只画边框） */
void lcd_draw_rectangle(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,uint16_t color)
{
    lcd_draw_line(x0, y0, x1, y0, color);
    lcd_draw_line(x0, y0, x0, y1, color);
    lcd_draw_line(x0, y1, x1, y1, color);
    lcd_draw_line(x1, y0, x1, y1, color);
}

/* 画圆（Bresenham 圆算法） */
void lcd_draw_circle(uint16_t x0, uint16_t y0, uint8_t r, uint16_t color)
{
    int a = 0, b = r;
    int di = 3 - (r << 1);

    while (a <= b) {
        lcd_draw_point(x0 + a, y0 - b, color);
        lcd_draw_point(x0 + b, y0 - a, color);
        lcd_draw_point(x0 + b, y0 + a, color);
        lcd_draw_point(x0 + a, y0 + b, color);
        lcd_draw_point(x0 - a, y0 + b, color);
        lcd_draw_point(x0 - b, y0 + a, color);
        lcd_draw_point(x0 - a, y0 - b, color);
        lcd_draw_point(x0 - b, y0 - a, color);
        a++;
        if (di < 0) di += 4 * a + 6;
        else {
            di += 10 + 4 * (a - b);
            b--;
        }
    }
}

/* 画实心圆：用水平线填充 */
void lcd_fill_circle(uint16_t center_x, uint16_t center_y, uint16_t radius, uint16_t color)
{
    if (radius == 0) {
        lcd_draw_point(center_x, center_y, color);
        return;
    }

    uint32_t imax = (radius * 707U) / 1000U + 1;
    uint32_t sqmax = (uint32_t)radius * (uint32_t)radius + radius / 2;
    int xr = radius;
    lcd_draw_hline(center_x - radius, center_y, 2 * radius + 1, color);

    for (uint32_t i = 1; i <= imax; i++) {
        if ((i * i + (uint32_t)xr * xr) > sqmax) {
            if (xr > (int)imax) {
                lcd_draw_hline(center_x - i + 1, center_y + xr, 2 * (i - 1) + 1, color);
                lcd_draw_hline(center_x - i + 1, center_y - xr, 2 * (i - 1) + 1, color);
            }
            xr--;
        }
        lcd_draw_hline(center_x - xr, center_y + i, 2 * xr + 1, color);
        lcd_draw_hline(center_x - xr, center_y - i, 2 * xr + 1, color);
    }
}

static uint32_t lcd_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while (n--) result *= m;
    return result;
}

/* 显示单个字符（size: 12/16/24/32） mode: 0=非叠加(背景填充), 1=叠加 */
void lcd_show_char(uint16_t x, uint16_t y, char chr, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t temp, t1, t;
    uint16_t y0 = y;
    uint8_t csize;
    const uint8_t *pfont = NULL;

    if (chr < ' ' || chr > '~') return;
    uint8_t index = (uint8_t)(chr - ' ');

    switch (size) {
        case 12: csize = 12 / 8 + ((12 % 8) ? 1 : 0); csize *= (12 / 2); pfont = (const uint8_t *)asc2_1206[index]; break;
        case 16: csize = 16 / 8 + ((16 % 8) ? 1 : 0); csize *= (16 / 2); pfont = (const uint8_t *)asc2_1608[index]; break;
        case 24: csize = 24 / 8 + ((24 % 8) ? 1 : 0); csize *= (24 / 2); pfont = (const uint8_t *)asc2_2412[index]; break;
        case 32: csize = 32 / 8 + ((32 % 8) ? 1 : 0); csize *= (32 / 2); pfont = (const uint8_t *)asc2_3216[index]; break;
        default: return;
    }

    for (t = 0; t < csize; t++) {
        temp = pfont[t];
        for (t1 = 0; t1 < 8; t1++) {
            if (temp & 0x80) {
                lcd_draw_point(x, y, color);
            } else if (mode == 0) {
                lcd_draw_point(x, y, (uint16_t)g_back_color);
            }
            temp <<= 1;
            y++;
            if ((y - y0) == size) { y = y0; x++; if (x >= lcddev.width) return; break; }
            if (y >= lcddev.height) return;
        }
    }
}

/* 显示 len 位数字，不显示前置 0（会用空格替代） */
void lcd_show_num(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;
    for (t = 0; t < len; t++) {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;
        if (enshow == 0 && t < (len - 1)) {
            if (temp == 0) {
                lcd_show_char(x + (size / 2) * t, y, ' ', size, 0, color);
                continue;
            } else enshow = 1;
        }
        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, 0, color);
    }
}

/* 显示 len 位数字（高位 0 可选填充） mode: bit7 填充0, bit0 叠加模式 */
void lcd_show_xnum(uint16_t x, uint16_t y, uint32_t num, uint8_t len, uint8_t size, uint8_t mode, uint16_t color)
{
    uint8_t t, temp;
    uint8_t enshow = 0;
    for (t = 0; t < len; t++) {
        temp = (num / lcd_pow(10, len - t - 1)) % 10;
        if (enshow == 0 && t < (len - 1)) {
            if (temp == 0) {
                if (mode & 0x80) lcd_show_char(x + (size / 2) * t, y, '0', size, mode & 0x01, color);
                else lcd_show_char(x + (size / 2) * t, y, ' ', size, mode & 0x01, color);
                continue;
            } else enshow = 1;
        }
        lcd_show_char(x + (size / 2) * t, y, temp + '0', size, mode & 0x01, color);
    }
}

/* 显示字符串（按 width/height 限制自动换行） */
void lcd_show_string(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint8_t size, char *p, uint16_t color)
{
    uint16_t x0 = x;
    uint16_t max_x = x + width;
    uint16_t max_y = y + height;

    while (*p >= ' ' && *p <= '~') {
        if (x >= max_x) { x = x0; y += size; }
        if (y >= max_y) break;
        lcd_show_char(x, y, *p, size, 0, color);
        x += size / 2;
        p++;
    }
}

