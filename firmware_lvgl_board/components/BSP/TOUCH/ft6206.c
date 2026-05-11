#include "ft6206.h"

static const char *TAG = "ft6206";

extern i2c_master_bus_handle_t bus_handle1; 
static i2c_master_dev_handle_t ft6206_handle = NULL;

#define FT6206_MAX_TOUCH_POINTS   2

esp_err_t ft6206_write_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    esp_err_t ret;
    uint8_t *write_buf = malloc(1 + len);
    if (write_buf == NULL) {
        ESP_LOGE(TAG, "Memory allocation failed");
        return ESP_ERR_NO_MEM;
    }

    write_buf[0] = reg;
    memcpy(write_buf + 1, buf, len);

    ret = i2c_master_transmit(ft6206_handle, write_buf, 1 + len, -1);
    free(write_buf);

    return ret;
}

esp_err_t ft6206_read_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    return i2c_master_transmit_receive(ft6206_handle, &reg, 1, buf, len, -1);
}

esp_err_t ft6206_init(void)
{
#if (FT6206_RST_GPIO_PIN != -1) 
    gpio_config_t rst_gpio_config = {
        .pin_bit_mask = (1ULL << FT6206_RST_GPIO_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_gpio_config);
#endif 

    if (bus_handle1 == NULL) 
    {
        ESP_LOGI(TAG, "I2C1 bus not initialized, initializing...");
        ESP_ERROR_CHECK(myiic_init1()); 
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT6206_DEVICE_ADDRESS,
        .scl_speed_hz = IIC_SPEED_CLK, 
    };
    
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle1, &dev_cfg, &ft6206_handle));

#if (FT6206_RST_GPIO_PIN != -1) 
    FT6206_RST_CTRL(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    FT6206_RST_CTRL(1);
    vTaskDelay(pdMS_TO_TICKS(100));
#else
    vTaskDelay(pdMS_TO_TICKS(120)); // 如果RST无效，也提供一个延时
#endif // <--- 修正 (4)

    uint8_t chip_id = 0;
    esp_err_t ret = ft6206_read_reg(FT6206_REG_CHIPID, &chip_id, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read chip ID");
        return ESP_FAIL;
    }

    if (chip_id == FT6206_CHIPID_VALUE || chip_id == FT6236_CHIPID_VALUE) {
        ESP_LOGI(TAG, "Found FT62xx chip, ID: 0x%02X", chip_id);
    } else {
        ESP_LOGE(TAG, "Touch chip not found, expected 0x%02X or 0x%02X, but got 0x%02X",
                 FT6206_CHIPID_VALUE, FT6236_CHIPID_VALUE, chip_id);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

uint8_t ft6206_scan(void)
{
    uint8_t touch_points_num = 0;
    uint8_t res = 0;

    ft6206_read_reg(FT6206_REG_TD_STATUS, &touch_points_num, 1);
    touch_points_num &= 0x0F;

    if (touch_points_num > 0 && touch_points_num <= FT6206_MAX_TOUCH_POINTS)
    {
        tp_dev.sta = TP_PRES_DOWN | TP_CATH_PRES;
        uint8_t data_buf[FT6206_MAX_TOUCH_POINTS * 6];
        
        ft6206_read_reg(FT6206_REG_P1_XH, data_buf, sizeof(data_buf));

        for (int i = 0; i < touch_points_num; i++)
        {
            uint8_t *p_buf = &data_buf[i * 6];
            
            uint16_t raw_x = (((uint16_t)(p_buf[0] & 0x0F) << 8) | p_buf[1]);
            uint16_t raw_y = (((uint16_t)(p_buf[2] & 0x0F) << 8) | p_buf[3]);

            tp_dev.x[i] = raw_x;
            tp_dev.y[i] = raw_y;
            
            tp_dev.sta |= (1 << i);
        }
        
        if (touch_points_num == 1) {
             tp_dev.x[0] = tp_dev.x[0];
             tp_dev.y[0] = tp_dev.y[0];
        }
        
        res = 1;
    }
    else
    {
        if (tp_dev.sta & TP_PRES_DOWN)
        {
            tp_dev.sta &= ~TP_PRES_DOWN;
        }
        else
        {
            tp_dev.x[0] = 0xFFFF;
            tp_dev.y[0] = 0xFFFF;
            tp_dev.sta &= 0xE000;
        }
    }

    return res;
}
