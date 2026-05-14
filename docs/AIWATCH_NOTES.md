# Ai Watch 固件 / 服务端 — 关键修改与易错点（承接说明）

本文档供后续 AI 或开发者快速理解：**音频、内存、HTTP、LVGL、配置** 相关约定与坑。

---

## 1. 启动顺序（避免黑屏重启）

问题：`BOARD_RECORD_MAX_BYTES` 等大块 **内部 RAM** 若在 LVGL **DMA 双缓冲** 之前分配，堆耗尽 → `lv_disp_draw_buf_init` 收到 NULL → 崩溃循环。

**当前顺序（`main.c`）**：

1. Wi‑Fi 连接  
2. **`lvgl_engine_init()`** — `lv_init`、`lv_port_disp_init`（DMA 缓冲）、触摸、`esp_timer` tick  
3. **`app_audio_init()`** — PCM 环缓冲 + ES8311 + 采集任务  
4. 延时 + **`app_audio_play_boot_chime()`**  
5. **`lvgl_start_demo_tasks()`** — `lv_demo_task` / `led_task`

---

## 2. Flash / PSRAM（16 + 8）与录音缓冲

商品页常见规格：**16MB Flash + 8MB OPI PSRAM**（ESP32-S3-WROOM）。  
芯片支持外置 RAM ≠ 固件已用：**必须在 `sdkconfig` / `sdkconfig.defaults` 里启用 `CONFIG_SPIRAM=y` 等项**（与同仓库 `firmware/sdkconfig.defaults` 一致：`MODE_OCT`、`80M`、`BOOT_INIT`、`USE_MALLOC`）。未启用时，`heap_caps_malloc(..., SPIRAM)` 始终失败，大块仍落 **内部 SRAM**，易出现先前遇到的堆紧张问题。

启用后重新 **`idf.py fullclean build`**；串口启动日志应出现 **PSRAM / SPIRAM** 初始化成功字样（具体句式随 IDF 版本略不同）。若 **无法启动**：少数板型需改 **`SPIRAM_SPEED_40M`** 或接线非 Octal（需对照原理图），再逐项用 menuconfig 排查。

**串口自检**：启动过程中常有 **`spiram` / `SPI RAM` / `external ram`** 等组件日志；`app_main` 结束前会再打一行 **`Heap: SPIRAM free=… KB`**（随固件版本）。若 **`CONFIG_SPIRAM off`** 会有明确 **`PSRAM not enabled`** 警告。

### 无 PSRAM（或未启用）时的行为

若 **`CONFIG_SPIRAM` 未打开**：PCM 只能用内部 `malloc`。

- **`BOARD_RECORD_MAX_BYTES`**（`board_config.h`）名义上限可调，但 **`alloc_pcm_ring()`** 对内部 RAM 有 **上限（约 160KB）**，更大尺寸会跳过，避免挤掉 LVGL/WiFi。  
- 分配顺序：先试 SPIRAM（若编译启用），再按尺寸递减 `malloc`。  
- 录音时长上限 ≈ `缓冲字节 / 32000` 秒（16 kHz、16 bit、单声道）。

---

## 3. 音频：`esp_codec_dev_write` 返回值

**成功时返回 `ESP_CODEC_DEV_OK`（数值 0），不是写入字节数。**  
若写成 `if (w <= 0) break`，第一次成功写入会被误判为失败 → **扬声器静音 / 开机铃不响**。

播放 tone / WAV 写循环应判断：`w != ESP_CODEC_DEV_OK`，成功则 `off += chunk`。

---

## 4. XL9555 功放极性（扬声器）

原理图里接到 NS4150 的可能是 **使能** 或 **静音**，有效电平可能相反。

- Menuconfig：**Ai Watch → `CONFIG_AIW_XL9555_SPK_HIGH_ENABLES_AMP`**（高电平是否等于喇叭通）。  
- 代码里用 **`aiw_speaker_amp_set(on)`** 统一封装 **`xl9555_pin_write(MUTE, …)`**，勿在多处硬编码电平。

---

## 5. I2S 采集块大小

`capture_task` 里 **`esp_codec_dev_read`** 使用与 DMA 对齐的长度（工程中为 **`AUDIO_CAP_CHUNK_BYTES` = 960**），避免与 `dma_frame_num` 等不匹配导致读失败、`REC 0 B`。

---

## 6. HTTP 上传 WAV（避免 `ESP_ERR_NO_MEM`）

上传 **不可** `malloc(44 + pcm_len)` 拼整包（堆紧张时失败）。

**当前做法**：`http_post_hdr_then_data()` — 栈上 44 字节 WAV 头，`esp_http_client_write` **分块**写已有 PCM 指针（如 8KB 一块），无大块连续分配。

---

## 7. 界面报错文案

上传失败时勿只显示 `esp_err_to_name` + 含糊的 “WiFi? URL?”。

- 使用 **`upload_fail_explain()`**：区分 **NO_MEM**、**ESP_FAIL** 等，并 **`app_http_base_url()`** 打出 **menuconfig 里配置的服务器根地址**，便于对照 PC IP/端口。

---

## 8. LVGL 中文与字体

工程使用 **`lv_font_simsun_16_cjk`（子集）**，缺字会显示为方块/乱码。

- 固定 UI 字符串要么用 **字库内已有字形**（曾改用繁体同义），要么扩展字体符号表。  
- **`CONFIG_LV_TXT_ENC_UTF8`** 需打开。

---

## 9. 语音对话（DeepSeek）链路

- **松手后**：固件 `POST /api/chat/from_wav`（WAV = 44B 头 + PCM），**不做**仅保存 `recordings/upload`。  
- PC 端 **ASR** → **`deepseek_client.chat_completion`** → JSON `{"transcript","reply"}`。  
- **ASR 二选一**（`server/app/stt.py`）：`server/.env` 里同时配置 **`BAIDU_API_KEY` + `BAIDU_SECRET_KEY`** 时走 **百度语音识别**（`baidu_asr.py`，无需 Hugging Face）；否则走 **faster-whisper**（需联网下模型或 `HF_ENDPOINT` 镜像）。  
- 固件用 **cJSON** 解析；HTTP **120s** 超时（`app_http.c`）。  
- **NVS `privacy`** 打开时不发往云端（与旧「仅上传 WAV」一致）。

## 10. 服务端控制台

- 浏览器访问：**`http://<跑 uvicorn 的电脑 IP>:8765/dashboard/`**（本机可用 `http://127.0.0.1:8765/dashboard/`）。  
- 固件 **`CONFIG_AIW_SERVER_BASE_URL`** 应为 **`http://IP:8765`**（**不要**带 `/dashboard/`）。  
- 录音 WAV 保存目录：`server/data/wav/`（具体以 `main.py` 为准）。

## 10.1 WiFi 自主配网（`firmware_lvgl_board`，带屏手表工程）

固件把 **WiFi 名称/密码** 和 **后端地址** 存在 Flash 的 **NVS** 里（命名空间 **`aiw`**，键名 **`wifi_ssid`**、**`wifi_pass`**、**`srv_url`**）。**`srv_url`** 形如 **`http://222.186.32.214:8765`**，**末尾不要加斜杠**。

### 上电时固件在做什么

1. **若 NVS 里已有 `wifi_ssid`**：只按 NVS 里的账号连该 WiFi（不再用 menuconfig 里默认的 iPhone 等）。
2. **若 NVS 里没有保存过 WiFi**：先用 menuconfig / `sdkconfig.defaults` 里的 **`CONFIG_AIW_WIFI_SSID` / `CONFIG_AIW_WIFI_PASSWORD`** 试连一次，**最长等 60 秒**。
3. **若仍连不上**：进入 **配网模式**：开发板自己开一个 **WiFi 热点（SoftAP）**，此时 **屏幕还不会进 LVGL 主界面**（配网成功重启后才会正常进表盘/对话）。

### 配网热点长什么样

- **热点名称（SSID）**：**`AiWatch-` + MAC 地址后 1 字节两位十六进制**（再 1 字节两位十六进制），例如 **`AiWatch-A1B2`**（以你板子实际串口打印为准）。
- **热点密码**：**`menuconfig → Ai Watch → SoftAP password`**，默认 **`12345678`**（可在 Kconfig 里改 **`CONFIG_AIW_PROV_AP_PASSWORD`**）。

串口里会出现类似：

`Provisioning SoftAP: SSID="AiWatch-xxYY" password="12345678" open http://192.168.4.1`

### 你（手机端）一步一步怎么操作

1. **用另一台手机或电脑**打开 **WLAN**，在列表里找到 **`AiWatch-xxxx`** 这个名字的热点（与串口日志一致）。
2. 输入密码（默认 **`12345678`**），**连接成功**。
3. 打开手机浏览器（Chrome / Safari 均可），地址栏输入：**`http://192.168.4.1`**（必须是 **192.168.4.1**，不要用百度或别的搜索框当网址）。
4. 页面打开后填写：
   - **WiFi 名称**：你要让手表长期连接的路由器或手机热点名称（**必须是 2.4GHz**，纯 5GHz 的 SSID 手表往往搜不到或连不上）。
   - **WiFi 密码**：该网络的密码。
   - **服务器地址**：默认已填 **`http://222.186.32.214:8765`**；若你换了域名或端口，改成你的 **`http://公网IP或域名:端口`**，同样**不要末尾斜杠**。
5. 点 **「保存并重启」**。开发板会 **重启**，自动用刚保存的账号去连你的路由器/热点；连上后才会跑 **LVGL、音频、对话** 等逻辑。
6. 若重启后仍进不了主界面：看 **串口日志** 是否仍打印 **`STA failed` / `connect timeout`**，多半是 **SSID/密码错误**、**路由器只有 5GHz**、或 **路由器拒绝新设备**。

### 只想「换 WiFi / 换服务器」——重新配网

任选其一即可：

- **方法一（最干净）**：在工程目录执行 **`idf.py erase-flash`** 再 **`idf.py flash`**，会清掉整片 Flash（含 NVS），相当于恢复出厂；上电后若 NVS 无记录，会先用 menuconfig 默认 WiFi 试 60 秒，失败再进热点配网。
- **方法二（只清 WiFi 记录）**：仍用 **`erase-flash`** 或后续增加「串口命令擦除 `aiw` 里 `wifi_ssid`」；当前仓库未做菜单项擦除，实用上 **`erase-flash`** 最简单。
- **方法三**：用 **esptool / idf.py** 只擦 NVS 分区（需你熟悉分区表 `partitions-16MiB.csv` 里 nvs 偏移，进阶用法）。

### 与 `firmware/main`（无屏控制台版）的关系

- **`firmware/main`** 里同样有一套 NVS + SoftAP + **`http://192.168.4.1`** 配网逻辑，与 **`firmware_lvgl_board`** 行为一致；你日常烧 **`firmware_lvgl_board`** 时以本节为准即可。

---

## 11. 串口自检日志（音频）

- **`pcm buffer … B`** — 缓冲分配成功及大致可录时长。  
- **`mic path smoke read ret=0`** — `ESP_CODEC_DEV_OK`，麦克风采音链路可读。  
- **`audio capture task started chunk=960B`** — 采集任务已创建。

---

## 12. `POST /api/chat/from_wav` 返回 500 / 手表卡在 Thinking

- **含义**：请求已到 PC（网络正常），但服务端在 **Whisper STT** 或 **DeepSeek / 写库** 阶段出错。  
- **排查**：看运行 `run_server.ps1` 的终端里 **`Traceback`** / **`STT failed`** / **`DeepSeek HTTP`** 日志（已加 `log.exception`）。  
- **常见原因**：未安装或未装好 **`faster-whisper`** / **`ctranslate2`**；`.env` 里 **`WHISPER_DEVICE=cuda`** 但本机无 CUDA；**`DEEPSEEK_API_KEY`** 无效导致 API 4xx（现为 **502** + JSON `error`）；首次下载 Whisper 模型失败或磁盘权限。

## 13. 常见串口 / 现象对照

| 现象 | 可能原因 |
|------|----------|
| `pcm buffer alloc failed` | 堆不足；关 SPIRAM 时勿强行要超大内部缓冲；或先保证 LVGL 已初始化 |
| `boot chime: codec not ready` | `app_audio_init` 失败或未执行 |
| `ESP_ERR_NO_MEM`（上传） | 已用流式上传；若仍出现，查 TLS/客户端缓冲或其它 `malloc` |
| I2S `channel has not been enabled yet` | codec 驱动 enable/disable 顺序噪声，若后续 smoke read 正常可暂忽略 |

---

*文档随代码演进请同步更新要点。*
