#include "touch.h"

_m_tp_dev tp_dev =
{
    .init = tp_init,
    .scan = 0,
    .x = {0},
    .y = {0},
    .sta = 0,
    .touchtype = 0x00
};

esp_err_t tp_init(void)
{
    esp_err_t ret;
    
    ret = ft6206_init();
    if (ret != ESP_OK)
    {
        return ret;
    }
    
    tp_dev.scan = ft6206_scan;
    tp_dev.touchtype |= 0x80;
    
    return ESP_OK;
}
