# Ai Watch — ESP32-S3 智能手表/助手（固件 + 本地服务端）

面向幻尔 ESP32-S3 V1.1 开发板：**板载 ES8311 麦克风与扬声器**、触摸屏、按键、WiFi；对话与推理通过 **DeepSeek API**（密钥放在服务端环境变量）。

## 目录结构

```
Ai Watch/
  docs/WIRING.md          # 接线说明（与官方例程管脚一致）
  firmware/               # ESP-IDF 工程（idf.py build / flash）
  server/                 # 本地 FastAPI 服务（DeepSeek、日程、录音元数据）
```

## 固件（ESP-IDF）

1. 安装 [ESP-IDF 5.x](https://docs.espressif.com/projects/esp-idf/) 并配置环境。
2. 接线见 `docs/WIRING.md`（默认仅用板载音频）；在 `firmware` 目录执行：

```bash
cd firmware
idf.py set-target esp32s3
idf.py menuconfig
```

在 **Ai Watch** 菜单中设置：WiFi SSID/密码、本地服务器 URL（默认 `http://192.168.1.100:8765`）。

```bash
idf.py build flash monitor
```

首次编译会从组件仓库拉取 **LVGL**、**esp_codec_dev**、**es8311**。

## 服务端（本机）

```bash
cd server
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
copy .env.example .env
# 编辑 .env，填入 DEEPSEEK_API_KEY
uvicorn app.main:app --host 0.0.0.0 --port 8765
```

设备与电脑需在同一局域网；防火墙放行 8765。

## 功能与后续扩展（当前框架）

| 模块 | 状态 |
|------|------|
| UI（LVGL） | 首页 / 录音 / 日程 / 复盘 / 设置（含隐私开关） |
| WiFi + HTTP JSON | 已实现 |
| 板载 ES8311 录音 + 上传 WAV | 已实现（按键启停，长度上限见 `board_config.h`） |
| 板载扬声器播放 | Codec 已初始化，可在同一路径增加 `esp_codec_dev_write` 做 TTS/回放 |
| 唤醒词 | 占位：按键 + 可选能量门限扩展 |
| 日程/提醒/检索 | 服务端 API + SQLite；设备端列表展示与占位请求 |

将 DeepSeek Key 只放在 `server/.env`，**不要**写入固件。
