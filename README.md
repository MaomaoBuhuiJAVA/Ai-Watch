# Ai Watch — ESP32-S3 固件 + 本地服务端

面向幻尔 ESP32-S3 V1.1：**当前固件为「无屏基础版」**——USB 串口人机对话 + **KEY1** 一键录音上传；**板载 ES8311** 麦/喇叭；对话走本机 **FastAPI** 再调 **DeepSeek**（密钥只在 `server/.env`）。

## 架构（当前）

```
┌─────────────────┐     WiFi HTTP      ┌──────────────────┐     HTTPS      ┌──────────┐
│  ESP32-S3       │ ─────────────────► │  PC: FastAPI     │ ─────────────► │ DeepSeek │
│  串口: 输入问题  │   /api/chat        │  /api/chat       │                │          │
│  KEY1: 录音开关 │   /api/recordings  │  SQLite + WAV    │                └──────────┘
└─────────────────┘                    └──────────────────┘
```

- **设备**：`app_wifi` → `app_http`；`app_console` 用 **`fgets(stdin)`**（USB 串口）发 `/api/chat`；`app_audio_codec` 用 ES8311 录音，停止后按 NVS 隐私标志上传 WAV。
- **服务端**：`server/app/main.py`（已有 `/api/chat`、`/api/recordings/upload` 等）。

后续要加 **LCD + LVGL**，可单独恢复组件与 `app_ui_lvgl`（会明显增加编译与链接时间）。

## 目录

```
Ai Watch/
  docs/WIRING.md
  firmware/          # ESP-IDF，无 LVGL
  server/
```

## 固件编译与烧录

1. 打开 **ESP-IDF 5.5.4** 终端，`cd firmware` 后执行 **`. .\export_build_env.ps1`**（中文 Windows 避免 kconfig 编码问题）。
2. **去掉旧 LVGL 残留**（若曾编过旧工程）：删除目录 **`firmware/managed_components/lvgl__lvgl`**（若存在），然后：

```powershell
idf.py fullclean
idf.py set-target esp32s3
idf.py menuconfig
```

在 **Ai Watch** 中设置 WiFi、`AIW_SERVER_BASE_URL`（电脑 IP:8765）。

```powershell
idf.py build flash monitor
```

3. 串口里会出现 `=== Ai Watch (no screen) ===`，输入一行回车即发起对话；**KEY1** 切换录音。

## 服务端

```powershell
cd server
python -m venv .venv
.\.venv\Scripts\activate
pip install -r requirements.txt
copy .env.example .env
# 填入 DEEPSEEK_API_KEY
uvicorn app.main:app --host 0.0.0.0 --port 8765
```

电脑与开发板同一局域网；防火墙放行 **8765**。

## 功能表（无屏版）

| 模块 | 说明 |
|------|------|
| 串口对话 | 每行文本 → `POST /api/chat` → 打印回复 |
| KEY1 | 启停录音；停止后上传 WAV（未开隐私时） |
| ES8311 | 板载麦/喇叭；XL9555 开 MUTE |
| 日程/转写等 | 仍在服务端 API，设备端后续再接 |

将 DeepSeek Key **只**放在 `server/.env`。
