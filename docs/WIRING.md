# 幻尔 ESP32-S3 开发板 V1.1 接线说明（当前：板载 ES8311 麦与喇叭）

管脚依据官方 IDF 教程（与《ESP32S3开发板原理图_V1.1》一致）：`3.4 多媒体/01_audio_es8311`、`3.5 WiFi` 等例程。

**当前仓库固件（无屏基础版）**不驱动 LCD/触摸屏，仅使用 **USB 串口、WiFi、ES8311、XL9555 按键**；屏与触摸硬件仍在板上，日后可加回 LVGL。

## 1. 板载音频（本工程默认，无需外接线）

| 功能 | 说明 |
|------|------|
| Codec ES8311 | I2C1：**GPIO4**（SDA）、**GPIO5**（SCL）；I2S0：MCLK **GPIO45**、BCLK **GPIO39**、WS **GPIO41**、ESP→Codec **GPIO42**（DOUT）、Codec→ESP **GPIO40**（DIN） |
| 麦克风 / 扬声器 | 走 ES8311；扬声器接到开发板标注的**喇叭/扬声器接口**（不要再焊 GPIO） |
| 静音 / 功放使能 | 经 IO 扩展芯片 XL9555 的 **MUTE** 引脚，固件会上拉打开扬声器 |

录音与播放均通过 `esp_codec_dev` + ES8311（见 `firmware/main/app_audio_codec.c`），与官方 `01_audio_es8311` 例程一致。

## 2. 其它板载外设（无需飞线）

| 功能 | 说明 |
|------|------|
| 触摸屏 FT62xx | I2C1：SDA **GPIO4**、SCL **GPIO5**（与 ES8311 共用 I2C 总线） |
| IO 扩展 XL9555 | I2C0：SDA **GPIO38**、SCL **GPIO48**；按键 KEY1–KEY4、背光、TF 片选等 |
| LCD（2.4 寸 ST7789 SPI） | SPI2：MOSI **GPIO47**、SCLK **GPIO21**、DC **GPIO3**、CS **GPIO2**；背光经 XL9555 |

## 3.（可选）日后外接 INMP441

若改用外接数字麦，需占用另一组 I2S（例如 I2S1）并自行改固件；此时与板载 ES8311 的 **I2S0** 不冲突。当前仓库默认未启用外接麦。

## 4. 电源与地

板载模拟电路按开发板手册供电；外设与开发板共地。
