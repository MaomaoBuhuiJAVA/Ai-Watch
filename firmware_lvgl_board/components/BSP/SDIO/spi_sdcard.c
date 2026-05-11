#include "spi_sdcard.h"
#include "esp_log.h"

/* 全局变量 */
sdmmc_card_t *card = NULL;
const char mount_point[] = MOUNT_POINT;

/* 日志标签 */
static const char *TAG = "SD_CARD";

/**
 * @brief       SD卡初始化并挂载文件系统
 * @param       无
 * @retval      esp_err_t: ESP_OK 表示成功, 其他表示失败
 */
esp_err_t sd_spi_init(void)
{
    esp_err_t ret;

    /* 文件系统挂载配置 */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    /* SD卡主机配置 (使用默认配置) */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    /* SD卡设备/插槽配置 */
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_NUM_CS;
    slot_config.host_id = host.slot;

    ESP_LOGI(TAG, "Mounting filesystem to %s...", mount_point);

    /* 调用单一函数完成初始化和挂载 */
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). Make sure SD card lines have pull-up resistors.", esp_err_to_name(ret));
        }
        return ret;
    }

    ESP_LOGI(TAG, "Filesystem mounted successfully.");

    /* 打印SD卡信息 */
    sdmmc_card_print_info(stdout, card);

    return ESP_OK;
}

/**
 * @brief       获取SD卡相关信息
 * @param       out_total_bytes：总大小
 * @param       out_free_bytes：剩余大小
 * @retval      无
 */
void sd_get_fatfs_usage(size_t *out_total_bytes, size_t *out_free_bytes)
{
    FATFS *fs;
    DWORD free_clusters;
    /* 注意: f_getfree 的第一个参数是驱动器号的字符串，比如 "0:" */
    if (f_getfree(mount_point, &free_clusters, &fs) == FR_OK)
    {
        size_t total_sectors = (fs->n_fatent - 2) * fs->csize;
        size_t free_sectors = free_clusters * fs->csize;
        
        /* 转换为 KB */
        if (out_total_bytes != NULL)
        {
            *out_total_bytes = total_sectors * fs->ssize / 1024;
        }

        if (out_free_bytes != NULL)
        {
            *out_free_bytes = free_sectors * fs->ssize / 1024;
        }
    }
}
