# 幻尔 ESP32-S3 开发板 V1.1 接线说明（当前：板载 ES8311 麦与喇叭）

管脚依据官方 IDF 教程（与《ESP32S3开发板原理图_V1.1》一致）：`3.4 多媒体/01_audio_es8311`、`3.5 WiFi` 等例程。

**当前仓库固件（无屏基础版）**不驱动 LCD/触摸屏，仅使用 **USB 串口、WiFi、ES8311、XL9555 按键**；屏与触摸硬件仍在板上，日后可加回 LVGL。

## 1. 板载音频（本工程默认，无需外接线）

| 功能 | 说明 |
|------|------|
| Codec ES8311 | I2C1：**GPIO4**（SDA）、**GPIO5**（SCL）；I2S0：MCLK **GPIO45**、BCLK **GPIO39**、WS **GPIO41**、ESP→Codec **GPIO42**（DOUT）、Codec→ESP **GPIO40**（DIN） |
| 麦克风 / 扬声器 | 走 ES8311；扬声器接到开发板标注的**喇叭/扬声器接口**（不要再焊 GPIO） |
| 静音 / 功放使能 | 经 IO 扩展芯片 **XL9555** 的一路 GPIO（原理图里常标 **MUTE** 或接到 NS4150 的 **/SD、SHDN、EN** 等），**有效电平以原理图为准** |

录音与播放均通过 `esp_codec_dev` + ES8311。LVGL 工程见 `firmware_lvgl_board/main/app_audio_codec.c`。

### 1.1 与《ESP32S3开发板原理图_V1.1》对照时的要点

PDF 若不在本仓库，请在你资料包中打开 **《ESP32S3开发板原理图_V1.1》**，重点查：

1. **ES8311** 与 **ESP32-S3** 的 **I2C1（SDA/SCL）**、**I2S0（MCLK/BCLK/LRCK/DIN/DOUT）** 是否与上表一致（与官方 `01_audio_es8311` 教程一致）。
2. **板载模拟麦** 是否接到 ES8311 的 **MICP/MICN**（或 MICIN），而不是悬空或只接了耳机麦座未焊接。
3. **功放（常见 NS4150）**：  
   - 若 XL9555 控制的是 **/SHDN、EN** 类“使能”脚，通常 **高电平 = 芯片工作、喇叭可响**。  
   - 若该脚名是 **MUTE** 且接到功放 **静音输入**，则可能是 **高 = 静音、低 = 出声**，与“使能”脚相反。  
4. 固件中 **`idf.py menuconfig` → Ai Watch → `XL9555 MUTE/功放脚：高电平=喇叭通`** 即对应上述第 3 点：**无声时取消勾选一次再编译烧录**，相当于交换“开声”电平。

### 1.2 软件自检

- 上电后串口应打印 **`mic path smoke read ret=0`**（`0` 即 `ESP_CODEC_DEV_OK`）。若为 **`ESP_CODEC_DEV_WRONG_STATE`**，表示 codec 输入未就绪，需查 I2S RX / `esp_codec_dev_open`。  
- 若 **`ret` 为其它错误**，重点查 **I2C 是否读到 ES8311（地址 0x30）**、**MCLK 是否输出**。

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
