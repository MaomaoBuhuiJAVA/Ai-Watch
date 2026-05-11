#ifndef __TOUCH_H
#define __TOUCH_H

#include "ft6206.h"
#include "ltdc.h"
#include "iic.h"

#define TP_PRES_DOWN    0x8000
#define TP_CATH_PRES    0x4000
#define CT_MAX_TOUCH    2

typedef struct
{
    esp_err_t (*init)(void);
    uint8_t (*scan)(void);
    uint16_t x[CT_MAX_TOUCH];
    uint16_t y[CT_MAX_TOUCH];
    uint16_t sta;
    uint8_t touchtype;
} _m_tp_dev;

extern _m_tp_dev tp_dev;

esp_err_t tp_init(void);

#endif
