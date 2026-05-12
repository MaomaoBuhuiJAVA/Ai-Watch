# Ai Watch: build desk UI if needed, start API, open Dashboard in browser
# Run from repo root: .\start_desk.ps1
# Dashboard: http://127.0.0.1:8765/dashboard/
# Dev UI only: npm run desk:dev -> http://127.0.0.1:5173

$ErrorActionPreference = "Stop"
if ($PSScriptRoot) {
    $Root = $PSScriptRoot
} else {
    $Root = Split-Path -Parent $MyInvocation.MyCommand.Path
}
Set-Location -LiteralPath $Root

$deskIndex = Join-Path -Path $Root -ChildPath "dist-desk\index.html"
if (-not (Test-Path -LiteralPath $deskIndex)) {
    Write-Host ">>> dist-desk missing, running npm run desk:build ..." -ForegroundColor Cyan
    $nm = Join-Path -Path $Root -ChildPath "node_modules"
    if (-not (Test-Path -LiteralPath $nm)) {
        npm install
    }
    npm run desk:build
    if (-not (Test-Path -LiteralPath $deskIndex)) {
        Write-Host "desk:build failed. Check Node.js and npm." -ForegroundColor Red
        exit 1
    }
}

$dashUrl = "http://127.0.0.1:8765/dashboard/"
Write-Host ">>> Dashboard: $dashUrl" -ForegroundColor Green
Write-Host ">>> Legacy desk: /desk | Browser opens in ~5s after server starts." -ForegroundColor DarkGray

$null = Start-Job -ScriptBlock {
    Start-Sleep -Seconds 5
    Start-Process "http://127.0.0.1:8765/dashboard/"
}

$serverDir = Join-Path -Path $Root -ChildPath "server"
Set-Location -LiteralPath $serverDir
# Word/PDF: run_server.ps1 installs python-docx+reportlab into .venv if missing; or run .\install_export_deps.ps1
& (Join-Path -Path $serverDir -ChildPath "run_server.ps1")
