# 方案 A：从幻尔 LVGL 教程 14 复制工程到 Ai Watch\firmware_lvgl_board，并叠加上 Ai Watch 的网络/音频层。
# 在「Ai Watch」仓库根目录的 PowerShell 中执行：  .\firmware_lvgl_scheme_a\COPY_AND_MERGE.ps1

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$DataRoot = (Resolve-Path (Join-Path $RepoRoot "..")).Path
# DataRoot 已是「开发板资料」根目录，教程在其下的 1.教程资料\...
$Tutorial = Join-Path $DataRoot "1.教程资料\5.LVGL开发教程\源码\14_lvgl_conversion_of_number"
$Out = Join-Path $RepoRoot "firmware_lvgl_board"
$SchemeMain = Join-Path $PSScriptRoot "main"
$FwMain = Join-Path $RepoRoot "firmware\main"

if (-not (Test-Path $Tutorial)) {
    Write-Error "找不到教程目录，请检查路径是否存在：`n  $Tutorial"
}

Write-Host "复制教程工程 -> $Out"
if (Test-Path $Out) {
    Remove-Item -Recurse -Force $Out
}
New-Item -ItemType Directory -Path $Out | Out-Null
robocopy $Tutorial $Out /E /XD build .git managed_components | Out-Null
if ($LASTEXITCODE -ge 8) {
    Write-Error "robocopy 失败，退出码 $LASTEXITCODE"
}

# 资料包里的 LVGL 往往缺少 src 源码与 env_support，无法用 ESP-IDF 5.x CMake 直接编过。
$lvglOut = Join-Path $Out "components\LVGL"
$lvglProbe = Join-Path $lvglOut "src\misc\lv_log.h"
if (-not (Test-Path $lvglProbe)) {
    Write-Host "教程 LVGL 不完整，正在下载官方 LVGL v8.3.10 覆盖 components\LVGL …"
    $zip = Join-Path $env:TEMP "lvgl-8.3.10.zip"
    $expanded = Join-Path $env:TEMP "lvgl-8.3.10"
    $expandedProbe = Join-Path $expanded "src\misc\lv_log.h"
    if (-not (Test-Path $expandedProbe)) {
        Invoke-WebRequest -Uri "https://github.com/lvgl/lvgl/archive/refs/tags/v8.3.10.zip" -OutFile $zip -UseBasicParsing
        Expand-Archive -LiteralPath $zip -DestinationPath $env:TEMP -Force
    }
    Remove-Item -Recurse -Force $lvglOut
    New-Item -ItemType Directory -Path $lvglOut | Out-Null
    robocopy $expanded $lvglOut /E | Out-Null
    if ($LASTEXITCODE -ge 8) { Write-Error "robocopy LVGL 失败 $LASTEXITCODE" }
}
# ESP-IDF 5.4：COMPONENT_LIB 在早期为 INTERFACE 时不能对 target_compile_definitions 使用 PUBLIC
$espCmake = Join-Path $lvglOut "env_support\cmake\esp.cmake"
if (Test-Path $espCmake) {
    $raw = Get-Content -LiteralPath $espCmake -Raw
    if ($raw -notmatch "_lvgl_comp_type") {
        $old = @'
endif()

target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DLV_CONF_INCLUDE_SIMPLE")

if(CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM)
  target_compile_definitions(${COMPONENT_LIB}
                             PUBLIC "-DLV_ATTRIBUTE_FAST_MEM=IRAM_ATTR")
endif()
'@
        $new = @'
endif()

get_target_property(_lvgl_comp_type ${COMPONENT_LIB} TYPE)
if(_lvgl_comp_type STREQUAL "INTERFACE_LIBRARY")
  target_compile_definitions(${COMPONENT_LIB} INTERFACE "-DLV_CONF_INCLUDE_SIMPLE")
else()
  target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DLV_CONF_INCLUDE_SIMPLE")
endif()

if(CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM)
  if(_lvgl_comp_type STREQUAL "INTERFACE_LIBRARY")
    target_compile_definitions(${COMPONENT_LIB}
                               INTERFACE "-DLV_ATTRIBUTE_FAST_MEM=IRAM_ATTR")
  else()
    target_compile_definitions(${COMPONENT_LIB}
                               PUBLIC "-DLV_ATTRIBUTE_FAST_MEM=IRAM_ATTR")
  endif()
endif()
'@
        Set-Content -LiteralPath $espCmake -Value ($raw.Replace($old, $new)) -NoNewline
    }
}

Write-Host "覆盖 main（合并入口 + CMake + Kconfig + idf_component）..."
Copy-Item (Join-Path $SchemeMain "main.c") (Join-Path $Out "main\main.c") -Force
Copy-Item (Join-Path $SchemeMain "CMakeLists.txt") (Join-Path $Out "main\CMakeLists.txt") -Force
Copy-Item (Join-Path $SchemeMain "idf_component.yml") (Join-Path $Out "main\idf_component.yml") -Force
Copy-Item (Join-Path $SchemeMain "Kconfig.projbuild") (Join-Path $Out "main\Kconfig.projbuild") -Force
Copy-Item (Join-Path $SchemeMain "board_config.h") (Join-Path $Out "main\board_config.h") -Force

$copyNames = @(
    "app_wifi.c", "app_wifi.h",
    "app_http.c", "app_http.h",
    "app_audio_codec.c", "app_audio_codec.h"
)
foreach ($n in $copyNames) {
    $src = Join-Path $FwMain $n
    if (-not (Test-Path $src)) {
        Write-Error "缺少源文件: $src （请确认 Ai Watch\firmware\main 里存在）"
    }
    Copy-Item $src (Join-Path $Out "main\$n") -Force
}

$exp = Join-Path $RepoRoot "firmware\export_build_env.ps1"
if (Test-Path $exp) {
    Copy-Item $exp (Join-Path $Out "export_build_env.ps1") -Force
    Copy-Item (Join-Path $RepoRoot "firmware\export_build_env.bat") (Join-Path $Out "export_build_env.bat") -Force
}

$sdkDef = Join-Path $Out "sdkconfig.defaults"
$overlay = Join-Path $PSScriptRoot "sdkconfig.defaults.overlay"
if (-not (Test-Path $sdkDef)) {
    New-Item -ItemType File -Path $sdkDef | Out-Null
}
Get-Content -LiteralPath $overlay | ForEach-Object {
    $line = $_.TrimEnd()
    if ($line -match '^\s*$' -or $line -match '^\s*#') { return }
    $key = ($line -split '=', 2)[0]
    if (-not (Select-String -Path $sdkDef -Pattern "^$([regex]::Escape($key))=" -Quiet)) {
        Add-Content -Path $sdkDef -Value $line
    }
}

Write-Host ""
Write-Host "完成。建议：工程路径含中文且很长时，可复制到短路径再编译（例如 C:\Users\你的用户名\esp\ai_watch_lvgl），"
Write-Host "并设 CCACHE_DISABLE=1 或使用 idf.py -DCCACHE_ENABLE=0 build，避免工具链路径编码问题。"
Write-Host ""
Write-Host "ESP-IDF 终端（先激活 IDF，且建议用 .espressif 下 idf5.4_py3.11_env 的 Python 跑 export）："
Write-Host "  cd `"$Out`"   # 或短路径副本"
Write-Host "  . .\export_build_env.ps1   # 中文 Windows 建议"
Write-Host "  idf.py set-target esp32s3"
Write-Host "  idf.py menuconfig            # 配置 Ai Watch WiFi / 服务器"
Write-Host "  `$env:CCACHE_DISABLE='1'; idf.py -DCCACHE_ENABLE=0 build flash monitor"
Write-Host ""
Write-Host "说明：屏与触摸仍走教程 BSP+LVGL；对话/录音可在 main\\APP 里扩展 app_http / app_audio。"
