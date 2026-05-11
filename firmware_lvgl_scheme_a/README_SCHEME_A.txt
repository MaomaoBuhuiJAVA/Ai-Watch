方案 A：以幻尔官方「14_lvgl_conversion_of_number」为底板，叠加上 Ai Watch 的 WiFi / HTTP / ES8311。

本目录文件：
  main\main.c          合并后的 app_main（先 WiFi+音频，再 lvgl_demo）
  main\CMakeLists.txt  在教程 main 上增加 app_wifi / app_http / app_audio 与依赖
  main\idf_component.yml  托管 es8311 + esp_codec_dev（与当前 Ai Watch\firmware 一致）
  main\Kconfig.projbuild   WiFi / 服务器（从 Ai Watch 复制）
  main\board_config.h      音频参数
  COPY_AND_MERGE.ps1     一键：复制教程工程 + 覆盖上述文件 + 复制网络音频 .c

使用前：
  1. 已安装 ESP-IDF 5.x，且能 idf.py build。
  2. 构建前在生成出的工程目录执行 firmware\export_build_env.ps1（若在中文路径下）。

生成工程默认路径（与脚本内一致）：
  父目录\Ai Watch\firmware_lvgl_board

重要（已在 COPY_AND_MERGE.ps1 里尽量自动化）：
  1) 资料包里的 LVGL 常不完整；脚本会在缺少 src 时自动拉取官方 LVGL v8.3.10，并修补 env_support/cmake/esp.cmake 以适配 ESP-IDF 5.4。
  2) sdkconfig.defaults.overlay 会合并字体 Montserrat 18、16MB Flash、自定义分区表 partitions-16MiB.csv。
  3) 若中文路径过长导致编译/链接异常，可将 firmware_lvgl_board 整份复制到短英文路径（例如 C:\Users\<你>\esp\ai_watch_lvgl），并设置 CCACHE_DISABLE=1 或使用 idf.py -DCCACHE_ENABLE=0 build。

Wi-Fi（固定 STA，默认连手机热点）：
  默认 SSID **iPhone**、密码 **12345678**（可在 menuconfig → Ai Watch 修改）。
  上电前请先打开手机个人热点；板子只做 STA 连接，不再开配网 SoftAP。
