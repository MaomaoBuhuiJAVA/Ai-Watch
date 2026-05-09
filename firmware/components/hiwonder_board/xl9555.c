#include "xl9555.h"
#include "esp_log.h"
#include <stdlib.h>

static const char *xl9555_tag = "xl9555";
i2c_master_dev_handle_t xl9555_handle = NULL;
static uint16_t s_output_cache = 0;

esp_err_t xl9555_read_byte(uint8_t *data, size_t len)
{
    uint8_t reg_addr = XL9555_INPUT_PORT0_REG;
    return i2c_master_transmit_receive(xl9555_handle, &reg_addr, 1, data, len, -1);
}

esp_err_t xl9555_write_byte(uint8_t reg, uint8_t *data, size_t len)
{
    uint8_t *buf = malloc(1 + len);
    if (buf == NULL) {
        ESP_LOGE(xl9555_tag, "malloc failed");
        return ESP_ERR_NO_MEM;
    }
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    esp_err_t ret = i2c_master_transmit(xl9555_handle, buf, len + 1, -1);
    free(buf);
    return ret;
}

uint16_t xl9555_pin_write(uint16_t pin, int val)
{
    if (val) {
        s_output_cache |= pin;
    } else {
        s_output_cache &= ~pin;
    }
    uint8_t w_data[2] = {s_output_cache & 0xFF, (s_output_cache >> 8) & 0xFF};
    xl9555_write_byte(XL9555_OUTPUT_PORT0_REG, w_data, 2);
    return s_output_cache;
}

int xl9555_pin_read(uint16_t pin)
{
    uint8_t r_data[2];
    xl9555_read_byte(r_data, 2);
    uint16_t ret = (uint16_t)(r_data[1] << 8) | r_data[0];
    return (ret & pin) ? 1 : 0;
}

void xl9555_ioconfig(uint16_t config_value)
{
    uint8_t data[2];
    esp_err_t err;
    data[0] = (uint8_t)(0xFF & config_value);
    data[1] = (uint8_t)(0xFF & (config_value >> 8));
    do {
        err = xl9555_write_byte(XL9555_CONFIG_PORT0_REG, data, 2);
        if (err != ESP_OK) {
            ESP_LOGE(xl9555_tag, "ioconfig failed: %d", err);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    } while (err != ESP_OK);
}

static void IRAM_ATTR xl9555_exit_gpio_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    if (gpio_num == XL9555_INT_IO) {
        esp_rom_delay_us(20000);
        if (gpio_get_level(XL9555_INT_IO) == 0) {
            /* 扩展 IO 中断占位 */
        }
    }
}

void xl9555_int_init(void)
{
    gpio_config_t gpio_init_struct = {
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
        .pin_bit_mask = 1ull << XL9555_INT_IO,
    };
    gpio_config(&gpio_init_struct);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(XL9555_INT_IO, xl9555_exit_gpio_isr_handler, (void *)XL9555_INT_IO);
}

esp_err_t xl9555_init(void)
{
    uint8_t r_data[2];
    if (bus_handle == NULL) {
        ESP_ERROR_CHECK(myiic_init());
    }
    i2c_device_config_t xl9555_i2c_dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz = IIC_SPEED_CLK,
        .device_address = XL9555_ADDR,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &xl9555_i2c_dev_conf, &xl9555_handle));
    xl9555_read_byte(r_data, 2);
    xl9555_ioconfig(0x03F2);
    vTaskDelay(pdMS_TO_TICKS(10));
    s_output_cache = 0;
    xl9555_pin_write(TF_CS_IO, 1);
    xl9555_pin_write(LCD_CS_IO, 1);
    xl9555_pin_write(BACKLIGHT_IO, 0);
    ESP_LOGI(xl9555_tag, "XL9555 init done.");
    return ESP_OK;
}

uint8_t xl9555_key_scan(uint8_t mode)
{
    uint8_t keyval = 0;
    static uint8_t key_up = 1;
    if (mode) {
        key_up = 1;
    }
    if (key_up && (KEY1 == 0 || KEY2 == 0 || KEY3 == 0 || KEY4 == 0)) {
        esp_rom_delay_us(100000);
        key_up = 0;
        if (KEY1 == 0) {
            keyval = KEY1_PRES;
        }
        if (KEY2 == 0) {
            keyval = KEY2_PRES;
        }
        if (KEY3 == 0) {
            keyval = KEY3_PRES;
        }
        if (KEY4 == 0) {
            keyval = KEY4_PRES;
        }
    } else if (KEY1 == 1 && KEY2 == 1 && KEY3 == 1 && KEY4 == 1) {
        key_up = 1;
    }
    return keyval;
}
