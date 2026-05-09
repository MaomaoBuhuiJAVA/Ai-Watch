#include "spi.h"
#include "esp_check.h"

void spi2_init(void)
{
    spi_bus_config_t spi_bus_conf = {
        .miso_io_num = SPI_MISO_GPIO_PIN,
        .mosi_io_num = SPI_MOSI_GPIO_PIN,
        .sclk_io_num = SPI_CLK_GPIO_PIN,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 320 * 240 * 2 + 8,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &spi_bus_conf, SPI_DMA_CH_AUTO));
}
