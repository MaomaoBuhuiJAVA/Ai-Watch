#include "iic.h"
#include "esp_check.h"

i2c_master_bus_handle_t bus_handle;
i2c_master_bus_handle_t bus_handle1;

esp_err_t myiic_init(void)
{
    if (bus_handle != NULL) {
        return ESP_OK;
    }
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = IIC_NUM_PORT,
        .scl_io_num = IIC_SCL_GPIO_PIN,
        .sda_io_num = IIC_SDA_GPIO_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &bus_handle);
}

esp_err_t myiic_init1(void)
{
    if (bus_handle1 != NULL) {
        return ESP_OK;
    }
    i2c_master_bus_config_t cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = IIC1_NUM_PORT,
        .scl_io_num = IIC1_SCL_GPIO_PIN,
        .sda_io_num = IIC1_SDA_GPIO_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&cfg, &bus_handle1);
}
