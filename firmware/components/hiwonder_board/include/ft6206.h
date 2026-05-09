#ifndef FT6206_H
#define FT6206_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "string.h"
#include "iic.h"
#include "xl9555.h"

#define FT6206_RST_GPIO_PIN         (-1)
#define FT6206_INT_READ()           (xl9555_pin_read(TP_INT_IO))
#define FT6206_RST_CTRL(level)      (gpio_set_level(FT6206_RST_GPIO_PIN, (level)))

#define FT6206_DEVICE_ADDRESS       0x38
#define FT6206_REG_TD_STATUS        0x02
#define FT6206_REG_P1_XH            0x03
#define FT6206_REG_CHIPID           0xA8
#define FT6206_CHIPID_VALUE         0x11
#define FT6236_CHIPID_VALUE         0x64

esp_err_t ft6206_init(void);
uint8_t ft6206_scan(void);
esp_err_t ft6206_write_reg(uint8_t reg, uint8_t *buf, uint8_t len);
esp_err_t ft6206_read_reg(uint8_t reg, uint8_t *buf, uint8_t len);

#endif
