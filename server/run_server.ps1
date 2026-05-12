$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# 始终用「将要跑 uvicorn 的同一个 python.exe」装依赖，避免系统 pip 装进别的环境
$pythonExe = $null
if (Test-Path ".\.venv\Scripts\python.exe") {
    $pythonExe = (Resolve-Path ".\.venv\Scripts\python.exe").Path
    & .\.venv\Scripts\Activate.ps1
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    $pythonExe = (Get-Command python).Path
} else {
    Write-Host "Python not found. Install Python 3.10+, then from server folder:" -ForegroundColor Red
    Write-Host "  python -m venv .venv" -ForegroundColor Yellow
    Write-Host "  .\.venv\Scripts\Activate.ps1" -ForegroundColor Yellow
    Write-Host "  python -m pip install -r requirements.txt" -ForegroundColor Yellow
    exit 1
}

Write-Host "Using Python: $pythonExe" -ForegroundColor DarkGray
Write-Host "Dashboard (React): http://<this-PC>:8765/dashboard/   |   Legacy: .../desk"
Write-Host "AI: set DEEPSEEK_API_KEY in server/.env (https://platform.deepseek.com) or voice/text replies return 503"
Write-Host "Voice: set BAIDU_API_KEY + BAIDU_SECRET_KEY in .env for Baidu ASR (no HuggingFace); else faster-whisper"
Write-Host "Firmware menuconfig: CONFIG_AIW_SERVER_BASE_URL e.g. http://192.168.1.5:8765"

# Word/PDF 导出：若未安装则用同一解释器 pip 安装（先试官方源，再清华、阿里云）
$null = & $pythonExe -c "import docx; import reportlab" 2>$null
if ($LASTEXITCODE -ne 0) {
    Write-Host ">>> Missing python-docx or reportlab — installing into this environment..." -ForegroundColor Yellow
    $attempts = @(
        @{ Name = "PyPI (default)"; Args = @() },
        @{ Name = "Tsinghua mirror"; Args = @("-i", "https://pypi.tuna.tsinghua.edu.cn/simple", "--trusted-host", "pypi.tuna.tsinghua.edu.cn") },
        @{ Name = "Aliyun mirror"; Args = @("-i", "https://mirrors.aliyun.com/pypi/simple/", "--trusted-host", "mirrors.aliyun.com") }
    )
    $installed = $false
    foreach ($a in $attempts) {
        Write-Host "    pip ($($a.Name))..." -ForegroundColor DarkGray
        & $pythonExe -m pip install python-docx "reportlab>=4.2.0" @($a.Args)
        if ($LASTEXITCODE -eq 0) {
            $installed = $true
            break
        }
    }
    $null = & $pythonExe -c "import docx; import reportlab" 2>$null
    if (-not $installed -or $LASTEXITCODE -ne 0) {
        Write-Host ">>> Word/PDF export will fail until packages install. Manual fix (same Python as above):" -ForegroundColor Red
        Write-Host "    $pythonExe -m pip install python-docx reportlab -i https://pypi.tuna.tsinghua.edu.cn/simple --trusted-host pypi.tuna.tsinghua.edu.cn" -ForegroundColor Yellow
    } else {
        Write-Host ">>> python-docx + reportlab OK." -ForegroundColor Green
    }
}

& $pythonExe -m uvicorn app.main:app --host 0.0.0.0 --port 8765
