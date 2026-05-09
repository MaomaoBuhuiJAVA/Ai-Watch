#ifndef IIC_H
#define IIC_H

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#define IIC_NUM_PORT       I2C_NUM_0
#define IIC_SPEED_CLK      400000
#define IIC_SDA_GPIO_PIN   GPIO_NUM_38
#define IIC_SCL_GPIO_PIN   GPIO_NUM_48

#define IIC1_NUM_PORT      I2C_NUM_1
#define IIC1_SDA_GPIO_PIN  GPIO_NUM_4
#define IIC1_SCL_GPIO_PIN  GPIO_NUM_5

extern i2c_master_bus_handle_t bus_handle;
extern i2c_master_bus_handle_t bus_handle1;

esp_err_t myiic_init(void);
esp_err_t myiic_init1(void);

#endif
