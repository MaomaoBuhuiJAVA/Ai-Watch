# Fix ESP-IDF kconfgen UnicodeDecodeError on Chinese Windows / paths with non-ASCII.
# Usage (ESP-IDF PowerShell): . .\export_build_env.ps1
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
Write-Host "PYTHONUTF8=1 PYTHONIOENCODING=utf-8 (run idf.py after this)"
