#ifndef XL9555_H
#define XL9555_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "iic.h"
#include <string.h>

#define XL9555_INT_IO               GPIO_NUM_46

#define XL9555_INPUT_PORT0_REG      0
#define XL9555_INPUT_PORT1_REG      1
#define XL9555_OUTPUT_PORT0_REG     2
#define XL9555_OUTPUT_PORT1_REG     3
#define XL9555_INVERSION_PORT0_REG  4
#define XL9555_INVERSION_PORT1_REG  5
#define XL9555_CONFIG_PORT0_REG     6
#define XL9555_CONFIG_PORT1_REG     7

#define XL9555_ADDR                 0x20

#define CAMERA_LED_IO               0x0001
#define IMU_INT1_IO                 0x0002
#define TF_CS_IO                    0x0008
#define KEY1_IO                     0x0010
#define KEY2_IO                     0x0020
#define KEY3_IO                     0x0040
#define KEY4_IO                     0x0080
#define AP_INT_IO                   0x0100
#define TP_INT_IO                   0x0200
#define LCD_CS_IO                   0x2000
#define BACKLIGHT_IO                0x4000
#define MUTE                        0x8000

#define KEY1                        xl9555_pin_read(KEY1_IO)
#define KEY2                        xl9555_pin_read(KEY2_IO)
#define KEY3                        xl9555_pin_read(KEY3_IO)
#define KEY4                        xl9555_pin_read(KEY4_IO)

#define KEY1_PRES                   2
#define KEY2_PRES                   3
#define KEY3_PRES                   4
#define KEY4_PRES                   5

esp_err_t xl9555_init(void);
int xl9555_pin_read(uint16_t pin);
uint16_t xl9555_pin_write(uint16_t pin, int val);
esp_err_t xl9555_read_byte(uint8_t *data, size_t len);
esp_err_t xl9555_write_byte(uint8_t reg, uint8_t *data, size_t len);
uint8_t xl9555_key_scan(uint8_t mode);
void xl9555_int_init(void);

#endif
