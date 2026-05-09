#ifndef SPI_H
#define SPI_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

#define SPI_MOSI_GPIO_PIN   GPIO_NUM_47
#define SPI_CLK_GPIO_PIN    GPIO_NUM_21
#define SPI_MISO_GPIO_PIN   GPIO_NUM_14

void spi2_init(void);

#endif
