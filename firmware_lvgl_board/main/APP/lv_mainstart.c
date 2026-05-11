#include "lv_mainstart.h"
#include "lvgl.h"
#include "ltdc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

static const char *btnm1_map[24] ={
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "0", "A", "B", "\n",
    "C", "D", "E", "\n",
    "F", LV_SYMBOL_CLOSE, LV_SYMBOL_OK, ""
};

#define LV_ATK_BIT_W          25
#define LV_ATK_BIT_H          35
#define LV_ATK_BIT_INDEX_H    20
#define LV_ATK_BIT_ROW_SPACE  22
#define LV_ATK_BIT_COUNT      32  // 31个二进制按键 + 1个蓝色清空按键

static char lv_bit_flag[LV_ATK_BIT_COUNT];
static lv_obj_t *lv_scale_cont;
static lv_obj_t *lv_btnmatrix;
static lv_obj_t *lv_bit_index[LV_ATK_BIT_COUNT];
static lv_obj_t *lv_bit_text[LV_ATK_BIT_COUNT];
static lv_obj_t *lv_bit_label[LV_ATK_BIT_COUNT];
static lv_obj_t *lv_bin;
static lv_obj_t *lv_dec;
static lv_obj_t *lv_hex;
static lv_obj_t *lv_oct;
static lv_obj_t *lv_bin_label;
static lv_obj_t *lv_dec_label;
static lv_obj_t *lv_hex_label;
static lv_obj_t *lv_oct_label;
static lv_obj_t *lv_middle;
static bool lv_keyboard_closing = false;  // 防止键盘关闭后立即触发点击事件
static bool lv_updating_from_button = false;  // 防止从二进制按键更新时触发输入框事件

static void lv_atk_bit_str_reverse(char str[])
{
    int n = (int)strlen(str);
    for (int i = 0; i < n / 2; i++)
    {
        char t = str[i];
        str[i] = str[n - i - 1];
        str[n - i - 1] = t;
    }
}

static long lv_atk_bit_bin_to_dec(const char *pbin)
{
    long r = 0;
    for (int i = 0; pbin[i] != 0; i++) r = (r << 1) + (pbin[i] - '0');
    return r;
}

static int lv_atk_bit_to_dec(lv_obj_t *parent)
{
    int j = 0;
    for (int i = 0; i < LV_ATK_BIT_COUNT; i++)
    {
        if (parent == lv_bit_text[i]) return j;
        j++;
    }
    return j;
}

static long lv_atk_dec_to_oct(long dec)
{
    long oct = 0, k = 1;
    while (dec)
    {
        oct += (dec % 8) * k;
        dec /= 8;
        k *= 10;
    }
    return oct;
}

static long lv_atk_bec_to_bin(long n)
{
    long r = 0, k = 1;
    while (n)
    {
        r += (n & 1) ? k : 0;
        k *= 10;
        n >>= 1;
    }
    return r;
}

// 将十进制数转换为二进制字符串（最多31位）
static void lv_atk_dec_to_bin_str(long n, char *bin_str, int max_len)
{
    if (n == 0)
    {
        bin_str[0] = '0';
        bin_str[1] = '\0';
        return;
    }
    
    char temp[32] = {0};
    int idx = 0;
    
    // 从低位到高位构建二进制字符串
    while (n > 0 && idx < max_len - 1)
    {
        temp[idx++] = (n & 1) ? '1' : '0';
        n >>= 1;
    }
    
    // 反转字符串（因为是从低位到高位构建的）
    for (int i = 0; i < idx; i++)
    {
        bin_str[i] = temp[idx - 1 - i];
    }
    bin_str[idx] = '\0';
}

static long lv_atk_hex_to_dex(char *s)
{
    return strtol(s, NULL, 16);
}

static long lv_atk_oct_to_dex(long n)
{
    long sum = 0, p = 1;
    while (n)
    {
        int d = (int)(n % 10);
        sum += d * p;
        p *= 8;
        n /= 10;
    }
    return sum;
}

// 根据二进制字符串更新二进制按键显示
static void lv_update_bit_buttons_from_bin_str(const char *bin_str)
{
    if (!bin_str || bin_str[0] == '\0')
    {
        // 如果字符串为空，将所有按键设置为0
        for (int i = 0; i < 31; i++)
        {
            lv_label_set_text(lv_bit_label[i], "0");
            lv_bit_flag[i] = '0';
        }
        return;
    }
    
    int len = strlen(bin_str);
    // 限制长度为31位
    if (len > 31) len = 31;
    
    // 更新31个二进制按键（从右到左，索引0是最低位）
    for (int i = 0; i < 31; i++)
    {
        if (i < len)
        {
            // 从字符串末尾开始读取（最低位在字符串末尾）
            char bit_char = bin_str[len - 1 - i];
            // 确保是有效的二进制字符
            if (bit_char == '1')
            {
                lv_label_set_text(lv_bit_label[i], "1");
                lv_bit_flag[i] = '1';
            }
            else
            {
                lv_label_set_text(lv_bit_label[i], "0");
                lv_bit_flag[i] = '0';
            }
        }
        else
        {
            // 超出输入长度的位设置为0
            lv_label_set_text(lv_bit_label[i], "0");
            lv_bit_flag[i] = '0';
        }
    }
}

static void lv_keyboard_close_timer_cb(lv_timer_t *timer)
{
    lv_keyboard_closing = false;
    lv_timer_del(timer);
}

static void lv_event_bit_map_handler(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *obj = lv_event_get_target(event);

    if(code == LV_EVENT_CLICKED)
    {
        static char lv_buf[32];
        int lv_bit_post = 0;
        char lv_str_buf[32];
        char lv_oct_buf[32];
        char lv_bit_flag_buf[LV_ATK_BIT_COUNT] = "00000000000000000000000000000000";

        if (lv_btnmatrix)
        {
            lv_obj_del(lv_btnmatrix);
            if (lv_middle) lv_textarea_set_cursor_click_pos(lv_middle, true);
            lv_middle = NULL;
            lv_btnmatrix = NULL;
        }

        // 设置标志，防止触发输入框的VALUE_CHANGED事件导致循环更新
        lv_updating_from_button = true;
        
        // 只清空输入框的文本，不清空placeholder（避免触发VALUE_CHANGED）
        lv_textarea_set_text(lv_bin, "");
        lv_textarea_set_text(lv_dec, "");
        lv_textarea_set_text(lv_hex, "");
        lv_textarea_set_text(lv_oct, "");

        lv_bit_post = lv_atk_bit_to_dec(obj);
        
        // 检查是否是蓝色清空按键（索引31）
        if (lv_bit_post == 31)
        {
            // 清空功能：将所有二进制按键（索引0-30）设置为0
            for (int i = 0; i < 31; i++)  // 只清空31个二进制按键
            {
                lv_label_set_text(lv_bit_label[i], "0");
                lv_bit_flag[i] = '0';
            }
            // 清空所有输入框的placeholder
            lv_textarea_set_placeholder_text(lv_bin, "");
            lv_textarea_set_placeholder_text(lv_dec, "");
            lv_textarea_set_placeholder_text(lv_hex, "");
            lv_textarea_set_placeholder_text(lv_oct, "");
            // 重置标志
            lv_updating_from_button = false;
            return;  // 清空后直接返回，不执行后续的更新逻辑
        }
        
        lv_snprintf(lv_buf, sizeof(lv_buf), "%s", lv_label_get_text(lv_bit_label[lv_bit_post]));

        if (strcmp(lv_buf, "1") == 0)
        {
            lv_label_set_text(lv_bit_label[lv_bit_post], "0");
            lv_bit_flag[lv_bit_post] = '0';
        }
        else if (strcmp(lv_buf, "0") == 0)
        {
            lv_label_set_text(lv_bit_label[lv_bit_post], "1");
            lv_bit_flag[lv_bit_post] = '1';
        }

        strncpy(lv_bit_flag_buf, lv_bit_flag, LV_ATK_BIT_COUNT - 1);
        lv_bit_flag_buf[LV_ATK_BIT_COUNT - 1] = '\0';

        lv_atk_bit_str_reverse(lv_bit_flag_buf);
        
        lv_textarea_set_placeholder_text(lv_bin, lv_bit_flag_buf);
        const char * sbin = lv_textarea_get_placeholder_text(lv_bin);
        lv_snprintf(lv_str_buf, sizeof(lv_str_buf), "%ld", lv_atk_bit_bin_to_dec(sbin));
        lv_textarea_set_placeholder_text(lv_dec, lv_str_buf);

        long vdec = strtol(lv_str_buf, NULL, 10);
        lv_snprintf(lv_oct_buf, sizeof(lv_oct_buf), "%ld", lv_atk_dec_to_oct(vdec));
        lv_textarea_set_placeholder_text(lv_oct, lv_oct_buf);

        lv_snprintf(lv_str_buf, sizeof(lv_str_buf), "0x%lX", lv_atk_bit_bin_to_dec(sbin));
        lv_textarea_set_placeholder_text(lv_hex, lv_str_buf);
        
        // 重置标志
        lv_updating_from_button = false;
    }
}

static void lv_btnmatrix_event_handler(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *obj = lv_event_get_target(event);

    if(code == LV_EVENT_VALUE_CHANGED)
    {
        const char *txt = lv_btnmatrix_get_btn_text(lv_btnmatrix, lv_btnmatrix_get_selected_btn(obj));
        if (txt == btnm1_map[22] || txt == btnm1_map[21])
        {
            lv_keyboard_closing = true;  // 设置标志，防止立即触发点击事件
            lv_obj_del(lv_btnmatrix);
            lv_btnmatrix = NULL;
            lv_middle = NULL;
            // 使用定时器延迟重置标志，避免误触
            lv_timer_create(lv_keyboard_close_timer_cb, 300, NULL);  // 300ms 延迟
        }
        for (int i = 0; i < 20; i++)
        {
            if (txt == btnm1_map[i])
            {
                if (lv_middle) lv_textarea_add_text(lv_middle, txt);
                break;
            }
        }
    }
}

static void lv_ta_cb_event_handler(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *ta = lv_event_get_target(event);

    if((code == LV_EVENT_CLICKED) && (lv_btnmatrix == NULL) && !lv_keyboard_closing)
    {
        lv_middle = NULL;
        lv_textarea_set_cursor_click_pos(ta, false);
        lv_btnmatrix = lv_btnmatrix_create(lv_scr_act());
        lv_btnmatrix_set_map(lv_btnmatrix, btnm1_map);
        lv_btnmatrix_set_btn_width(lv_btnmatrix, 12, 1);
        // 增大键盘高度，使按钮更大，触摸区域更大
        lv_obj_set_size(lv_btnmatrix, lcddev.width, lcddev.height * 0.65);
        lv_obj_align(lv_btnmatrix, LV_ALIGN_BOTTOM_MID, 0, 0);
        // 增加按钮之间的间距（行间距和列间距）
        lv_obj_set_style_pad_row(lv_btnmatrix, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_column(lv_btnmatrix, 10, LV_PART_MAIN);
        // 增加每个按钮的内边距，使触摸区域更大
        lv_obj_set_style_pad_all(lv_btnmatrix, 15, LV_PART_ITEMS);
        // 增大按钮文字字体，使按钮更易识别
        lv_obj_set_style_text_font(lv_btnmatrix, &lv_font_montserrat_18, LV_PART_ITEMS);
        lv_obj_add_event_cb(lv_btnmatrix, lv_btnmatrix_event_handler, LV_EVENT_ALL, NULL);

        lv_textarea_set_text(lv_bin, "");
        lv_textarea_set_text(lv_dec, "");
        lv_textarea_set_text(lv_hex, "");
        lv_textarea_set_text(lv_oct, "");
        lv_textarea_set_placeholder_text(lv_bin, "");
        lv_textarea_set_placeholder_text(lv_dec, "");
        lv_textarea_set_placeholder_text(lv_hex, "");
        lv_textarea_set_placeholder_text(lv_oct, "");

        if (ta == lv_bin)
        {
            for (int i = 0 ; i < 18 ; i++)
            {
                if (i == 0 || i == 9 || i == 16 || i == 17) continue;
                lv_btnmatrix_set_btn_ctrl(lv_btnmatrix, i, LV_BTNMATRIX_CTRL_DISABLED);
            }
            lv_middle = lv_bin;
            lv_textarea_set_cursor_click_pos(lv_dec, true);
            lv_textarea_set_cursor_click_pos(lv_hex, true);
            lv_textarea_set_cursor_click_pos(lv_oct, true);
        }
        else if (ta == lv_dec)
        {
            for (int i = 0 ; i < 18 ; i++)
            {
                if (i == 10 || i == 11 || i == 12 || i == 13 || i == 14 || i == 15)
                    lv_btnmatrix_set_btn_ctrl(lv_btnmatrix, i, LV_BTNMATRIX_CTRL_DISABLED);
            }
            lv_middle = lv_dec;
            lv_textarea_set_cursor_click_pos(lv_bin, true);
            lv_textarea_set_cursor_click_pos(lv_hex, true);
            lv_textarea_set_cursor_click_pos(lv_oct, true);
        }
        else if (ta == lv_hex)
        {
            for (int i = 0; i < 19; i++)
                lv_btnmatrix_set_btn_ctrl(lv_btnmatrix, i, LV_BTNMATRIX_CTRL_CHECKABLE);
            lv_middle = lv_hex;
            lv_textarea_set_cursor_click_pos(lv_bin, true);
            lv_textarea_set_cursor_click_pos(lv_dec, true);
            lv_textarea_set_cursor_click_pos(lv_oct, true);
        }
        else if (ta == lv_oct)
        {
            for (int i = 0; i < 18; i++)
            {
                if (i > 6 && i < 16)
                    lv_btnmatrix_set_btn_ctrl(lv_btnmatrix, i, LV_BTNMATRIX_CTRL_DISABLED);
            }
            lv_middle = lv_oct;
            lv_textarea_set_cursor_click_pos(lv_bin, true);
            lv_textarea_set_cursor_click_pos(lv_dec, true);
            lv_textarea_set_cursor_click_pos(lv_hex, true);
        }
    }
    else if(code == LV_EVENT_VALUE_CHANGED)
    {
        // 如果正在从二进制按键更新，不处理输入框的VALUE_CHANGED事件
        if (lv_updating_from_button)
        {
            return;
        }
        
        char str_tmp[32];
        char lv_oct_buf[32];
        char lv_dex_oct_buf[32];
        static char regbit_flag_buf[32];
        const char * s = lv_textarea_get_text(ta);

        if (ta == lv_bin)
        {
            if (s && s[0])
            {
                // 更新二进制按键显示
                lv_update_bit_buttons_from_bin_str(s);
                
                memset(lv_oct_buf, 0, sizeof(lv_oct_buf));
                lv_snprintf(str_tmp, sizeof(str_tmp), "%ld", lv_atk_bit_bin_to_dec(s));
                lv_textarea_set_placeholder_text(lv_dec, str_tmp);
                long v = strtol(str_tmp, NULL, 10);
                lv_snprintf(lv_oct_buf, sizeof(lv_oct_buf), "%ld", lv_atk_dec_to_oct(v));
                lv_textarea_set_placeholder_text(lv_oct, lv_oct_buf);
                lv_snprintf(str_tmp, sizeof(str_tmp), "0x%lX", lv_atk_bit_bin_to_dec(s));
                lv_textarea_set_placeholder_text(lv_hex, str_tmp);
            }
            else
            {
                // 清空时，将所有二进制按键设置为0
                lv_update_bit_buttons_from_bin_str("");
                lv_textarea_set_placeholder_text(lv_dec, "");
                lv_textarea_set_placeholder_text(lv_hex, "");
                lv_textarea_set_placeholder_text(lv_oct, "");
            }
        }
        else if (ta == lv_dec)
        {
            if (s && s[0])
            {
                memset(regbit_flag_buf, 0, sizeof(regbit_flag_buf));
                memset(lv_oct_buf, 0, sizeof(lv_oct_buf));
                long v = strtol(s, NULL, 10);
                // 使用新函数直接转换为二进制字符串，避免溢出
                lv_atk_dec_to_bin_str(v, regbit_flag_buf, sizeof(regbit_flag_buf));
                
                // 更新二进制按键显示
                lv_update_bit_buttons_from_bin_str(regbit_flag_buf);
                
                lv_textarea_set_placeholder_text(lv_bin, regbit_flag_buf);
                lv_snprintf(lv_oct_buf, sizeof(lv_oct_buf), "%ld", lv_atk_dec_to_oct(v));
                lv_textarea_set_placeholder_text(lv_oct, lv_oct_buf);
                lv_snprintf(str_tmp, sizeof(str_tmp),"0x%lX", v);
                lv_textarea_set_placeholder_text(lv_hex, str_tmp);
            }
            else
            {
                // 清空时，将所有二进制按键设置为0
                lv_update_bit_buttons_from_bin_str("");
                lv_textarea_set_placeholder_text(lv_bin, "");
                lv_textarea_set_placeholder_text(lv_hex, "");
                lv_textarea_set_placeholder_text(lv_oct, "");
            }
        }
        else if (ta == lv_hex)
        {
            if (s && s[0])
            {
                memset(lv_oct_buf, 0, sizeof(lv_oct_buf));
                memset(regbit_flag_buf, 0, sizeof(regbit_flag_buf));
                memset(lv_dex_oct_buf, 0, sizeof(lv_dex_oct_buf));
                long v10 = lv_atk_hex_to_dex((char *)s);
                lv_snprintf(lv_oct_buf, sizeof(lv_oct_buf), "%ld", v10);
                lv_textarea_set_placeholder_text(lv_dec, lv_oct_buf);
                // 使用新函数直接转换为二进制字符串，避免溢出
                lv_atk_dec_to_bin_str(v10, regbit_flag_buf, sizeof(regbit_flag_buf));
                
                // 更新二进制按键显示
                lv_update_bit_buttons_from_bin_str(regbit_flag_buf);
                
                lv_textarea_set_placeholder_text(lv_bin, regbit_flag_buf);
                lv_snprintf(lv_dex_oct_buf, sizeof(lv_dex_oct_buf), "%ld", lv_atk_dec_to_oct(v10));
                lv_textarea_set_placeholder_text(lv_oct, lv_dex_oct_buf);
                lv_snprintf(str_tmp, sizeof(str_tmp), "0x%lX", v10);
                lv_textarea_set_placeholder_text(lv_hex, str_tmp);
            }
            else
            {
                // 清空时，将所有二进制按键设置为0
                lv_update_bit_buttons_from_bin_str("");
                lv_textarea_set_placeholder_text(lv_dec, "");
                lv_textarea_set_placeholder_text(lv_bin, "");
                lv_textarea_set_placeholder_text(lv_oct, "");
            }
        }
        else if (ta == lv_oct)
        {
            if (s && s[0])
            {
                memset(lv_oct_buf, 0, sizeof(lv_oct_buf));
                memset(regbit_flag_buf, 0, sizeof(regbit_flag_buf));
                long vo = strtol(s, NULL, 10);
                long v10 = lv_atk_oct_to_dex(vo);
                lv_snprintf(lv_oct_buf, sizeof(lv_oct_buf), "%ld", v10);
                lv_textarea_set_placeholder_text(lv_dec, lv_oct_buf);
                // 使用新函数直接转换为二进制字符串，避免溢出
                lv_atk_dec_to_bin_str(v10, regbit_flag_buf, sizeof(regbit_flag_buf));
                
                // 更新二进制按键显示
                lv_update_bit_buttons_from_bin_str(regbit_flag_buf);
                
                lv_textarea_set_placeholder_text(lv_bin, regbit_flag_buf);
                lv_snprintf(str_tmp, sizeof(str_tmp), "0x%lX", v10);
                lv_textarea_set_placeholder_text(lv_hex, str_tmp);
            }
            else
            {
                // 清空时，将所有二进制按键设置为0
                lv_update_bit_buttons_from_bin_str("");
                lv_textarea_set_placeholder_text(lv_dec, "");
                lv_textarea_set_placeholder_text(lv_bin, "");
                lv_textarea_set_placeholder_text(lv_hex, "");
            }
        }
    }
}

void lv_atk_scale_bit_init(lv_obj_t *parent)
{
    int cell_w = (lcddev.width - 6) / 16;
    if(cell_w < 12) cell_w = 12;
    int idx_h = LV_ATK_BIT_INDEX_H;
    int btn_h = LV_ATK_BIT_H;
    int x0 = 3;
    int y_idx0 = 30;
    int y_idx1 = 120;

    for (int i = 0; i < LV_ATK_BIT_COUNT; i++)
    {
        int row, col, x, y_index, y_btn;
        
        if (i < 31)  // 前31个是二进制按键
        {
            if (i < 16)  // 第一行：索引0-15，共16个
            {
                row = 0;
                col = 15 - i;  // 从右到左：15, 14, 13, ..., 0
            }
            else  // 第二行：索引16-30，共15个
            {
                row = 1;
                col = 15 - (i - 16);  // 从右到左：15, 14, 13, ..., 1
            }
            x = x0 + col * cell_w;
            y_index = (row == 0) ? y_idx0 : y_idx1;
            y_btn = y_index + idx_h;
        }
        else  // 索引31是蓝色清空按键，放在第二行最左侧（列0）
        {
            row = 1;
            col = 0;  // 第二行最左侧
            x = x0 + col * cell_w;
            y_index = y_idx1;
            y_btn = y_index + idx_h;
        }

        lv_bit_index[i] = lv_obj_create(parent);
        lv_obj_set_style_bg_color(lv_bit_index[i], lv_color_hex3(0x00), LV_PART_MAIN);
        lv_obj_set_style_border_side(lv_bit_index[i], 0, LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_bit_index[i], lv_color_hex3(0xffffff), LV_PART_MAIN);
        lv_obj_set_style_radius(lv_bit_index[i], 2, LV_PART_MAIN);
        lv_obj_set_size(lv_bit_index[i], cell_w - 2, idx_h);
        lv_obj_set_pos(lv_bit_index[i], x, y_index);

        lv_bit_text[i] = lv_btn_create(parent);
        lv_obj_set_style_radius(lv_bit_text[i], 5, LV_PART_MAIN);
        lv_obj_set_style_bg_color(lv_bit_text[i], lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        lv_obj_set_style_border_side(lv_bit_text[i], 0, LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_bit_text[i], lv_color_hex3(0xffffff), LV_PART_MAIN);
        lv_obj_set_size(lv_bit_text[i], cell_w - 2, btn_h);
        lv_obj_set_pos(lv_bit_text[i], x, y_btn);
        lv_obj_add_event_cb(lv_bit_text[i], lv_event_bit_map_handler, LV_EVENT_ALL, NULL);

        lv_bit_label[i] = lv_label_create(lv_bit_text[i]);
        lv_label_set_text(lv_bit_label[i], "0");
        lv_obj_center(lv_bit_label[i]);

        if (i == 31)  // 索引31是蓝色清空按键
        {
            lv_obj_set_style_bg_color(lv_bit_text[31], lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
            lv_obj_set_style_bg_color(lv_bit_index[31], lv_palette_main(LV_PALETTE_BLUE), LV_PART_MAIN);
            lv_label_set_text(lv_bit_label[31], "");  // 清空按键不显示文本
        }
    }

    // BIN输入框位置和大小（与二进制按键左对齐，x0 = 3）
    int bin_x = x0;  // 与二进制按键左对齐
    int bin_y = y_idx1 + btn_h + 20 + 20 + 20;
    int bin_w = (int)(lcddev.width * 0.75) + 30;  // 宽度：75%屏幕宽度 + 30像素
    
    // BIN标签放在输入框外部左上角（在输入框上方）
    lv_bin_label = lv_label_create(parent);
    lv_label_set_text(lv_bin_label, "BIN");
    lv_obj_set_pos(lv_bin_label, bin_x, bin_y - 18);  // 输入框外部左上角，在输入框上方
    
    lv_bin = lv_textarea_create(parent);
    lv_textarea_set_accepted_chars(lv_bin, "01");
    lv_obj_set_style_text_font(lv_bin, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_radius(lv_bin, 0, LV_PART_MAIN);
    lv_obj_set_size(lv_bin, bin_w, LV_ATK_BIT_H);
    lv_textarea_set_text(lv_bin, "");
    lv_textarea_set_one_line(lv_bin, true);
    lv_textarea_set_cursor_click_pos(lv_bin, true);
    lv_obj_add_event_cb(lv_bin, lv_ta_cb_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_pos(lv_bin, bin_x, bin_y);

    // DEC输入框位置和大小（与二进制按键左对齐，x0 = 3）
    int dec_x = x0;  // 与二进制按键左对齐
    int dec_y = y_idx1 + btn_h + 20 + 20 + LV_ATK_BIT_H + 20 + 20;
    int dec_w = (int)(lcddev.width * 0.75) + 30;  // 宽度：75%屏幕宽度 + 30像素
    
    // DEC标签放在输入框外部左上角（在输入框上方）
    lv_dec_label = lv_label_create(parent);
    lv_label_set_text(lv_dec_label, "DEC");
    lv_obj_set_pos(lv_dec_label, dec_x, dec_y - 18);  // 输入框外部左上角，在输入框上方
    
    lv_dec = lv_textarea_create(parent);
    lv_textarea_set_accepted_chars(lv_dec, "0123456789");
    lv_obj_set_style_text_font(lv_dec, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_radius(lv_dec, 0, LV_PART_MAIN);
    lv_obj_set_size(lv_dec, dec_w, LV_ATK_BIT_H);
    lv_textarea_set_text(lv_dec, "");
    lv_textarea_set_one_line(lv_dec, true);
    lv_textarea_set_cursor_click_pos(lv_dec, true);
    lv_obj_add_event_cb(lv_dec, lv_ta_cb_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_pos(lv_dec, dec_x, dec_y);

    // HEX输入框位置和大小
    int hex_x = lcddev.width/2 + 50;
    int hex_y = y_idx1 + btn_h + 20 + 20 + 20;
    int hex_w = lcddev.width / 2 - 60;
    
    // HEX标签放在输入框外部左上角（在输入框上方）
    lv_hex_label = lv_label_create(parent);
    lv_label_set_text(lv_hex_label,"HEX");
    lv_obj_set_pos(lv_hex_label, hex_x, hex_y - 18);  // 输入框外部左上角，在输入框上方
    
    lv_hex = lv_textarea_create(parent);
    lv_textarea_set_accepted_chars(lv_hex, "0123456789ABCDEFabcdef");
    lv_obj_set_style_text_font(lv_hex, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_radius(lv_hex, 0, LV_PART_MAIN);
    lv_obj_set_size(lv_hex, hex_w, LV_ATK_BIT_H);
    lv_textarea_set_text(lv_hex, "");
    lv_textarea_set_one_line(lv_hex, true);
    lv_textarea_set_cursor_click_pos(lv_hex, true);
    lv_obj_add_event_cb(lv_hex, lv_ta_cb_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_pos(lv_hex, hex_x, hex_y);

    // OCT输入框位置和大小
    int oct_x = lcddev.width/2 + 50;
    int oct_y = y_idx1 + btn_h + 20 + 20 + LV_ATK_BIT_H + 20 + 20;
    int oct_w = lcddev.width / 2 - 60;
    
    // OCT标签放在输入框外部左上角（在输入框上方）
    lv_oct_label = lv_label_create(parent);
    lv_label_set_text(lv_oct_label, "OCT");
    lv_obj_set_pos(lv_oct_label, oct_x, oct_y - 18);  // 输入框外部左上角，在输入框上方
    
    lv_oct = lv_textarea_create(parent);
    lv_textarea_set_accepted_chars(lv_oct, "01234567");
    lv_obj_set_style_text_font(lv_oct, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_radius(lv_oct, 0, LV_PART_MAIN);
    lv_obj_set_size(lv_oct, oct_w, LV_ATK_BIT_H);
    lv_textarea_set_text(lv_oct, "");
    lv_textarea_set_one_line(lv_oct, true);
    lv_textarea_set_cursor_click_pos(lv_oct, true);
    lv_obj_add_event_cb(lv_oct, lv_ta_cb_event_handler, LV_EVENT_ALL, NULL);
    lv_obj_set_pos(lv_oct, oct_x, oct_y);
}


void lv_mainstart(void)
{
    // 初始化31个二进制按键的标志（索引0-30），索引31是蓝色清空按键不使用
    memset(lv_bit_flag, '0', 31);  // 前31个设置为'0'
    lv_bit_flag[31] = '\0';  // 索引31不使用，设置为结束符
    lv_scale_cont = lv_obj_create(lv_scr_act());
    lv_obj_set_size(lv_scale_cont, lcddev.width, lcddev.height);
    lv_obj_set_pos(lv_scale_cont, 0, 0);
    lv_btnmatrix = NULL;
    lv_atk_scale_bit_init(lv_scale_cont);
}
