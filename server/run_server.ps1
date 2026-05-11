$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
if (Test-Path ".venv\Scripts\Activate.ps1") {
    & .\.venv\Scripts\Activate.ps1
} elseif (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    Write-Host "Python not found. Install Python 3.10+, then: python -m venv .venv ; .\.venv\Scripts\pip install -r requirements.txt"
    exit 1
}
Write-Host "Uvicorn http://0.0.0.0:8765  |  service desk: http://<this-PC-LAN-IP>:8765/desk"
Write-Host "Voice: set BAIDU_API_KEY + BAIDU_SECRET_KEY in .env for Baidu ASR (no HuggingFace); else faster-whisper"
Write-Host "pip install -r requirements.txt"
Write-Host "Firmware menuconfig: CONFIG_AIW_SERVER_BASE_URL e.g. http://192.168.1.5:8765"
python -m uvicorn app.main:app --host 0.0.0.0 --port 8765
