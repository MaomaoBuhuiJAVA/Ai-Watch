#ifndef __SPI_SDCARD_H
#define __SPI_SDCARD_H

#include <unistd.h>
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "spi.h"
/* 引脚定义 */
#define SD_NUM_CS       GPIO_NUM_0
#define MOUNT_POINT     "/0:"
extern sdmmc_card_t *card;                                                 /* SD / MMC卡结构 */

/* 函数声明 */
esp_err_t sd_spi_init(void);                                                /* SD卡初始化 */
void sd_get_fatfs_usage(size_t *out_total_bytes, size_t *out_free_bytes);   /* 获取SD卡相关信息 */

#endif
