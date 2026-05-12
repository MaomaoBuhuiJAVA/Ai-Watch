# One-shot: install Word/PDF export deps into the SAME Python as run_server.ps1 uses.
# Run from repo:  cd server ; .\install_export_deps.ps1
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (Test-Path ".\.venv\Scripts\python.exe") {
    $pythonExe = (Resolve-Path ".\.venv\Scripts\python.exe").Path
} elseif (Get-Command python -ErrorAction SilentlyContinue) {
    $pythonExe = (Get-Command python).Path
} else {
    Write-Host "Python not found. Install Python 3.10+ or create server\.venv first." -ForegroundColor Red
    exit 1
}

Write-Host "Target: $pythonExe" -ForegroundColor Cyan
$attempts = @(
    @{ Name = "PyPI"; Args = @() },
    @{ Name = "Tsinghua"; Args = @("-i", "https://pypi.tuna.tsinghua.edu.cn/simple", "--trusted-host", "pypi.tuna.tsinghua.edu.cn") },
    @{ Name = "Aliyun"; Args = @("-i", "https://mirrors.aliyun.com/pypi/simple/", "--trusted-host", "mirrors.aliyun.com") }
)
foreach ($a in $attempts) {
    Write-Host "Trying $($a.Name)..." -ForegroundColor DarkGray
    & $pythonExe -m pip install -U python-docx "reportlab>=4.2.0" @($a.Args)
    if ($LASTEXITCODE -eq 0) { break }
}

& $pythonExe -c "import docx; import reportlab; print('OK: imports work')"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Import check failed. If all mirrors blocked, download .whl from https://pypi.org on another network and: pip install path\to\*.whl" -ForegroundColor Red
    exit 1
}
Write-Host "Done." -ForegroundColor Green
