# ESP-IDF on Windows: kconfgen reads build/config.env as UTF-8; without this,
# Chinese project paths cause UnicodeDecodeError: 'gbk' codec can't decode...
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
if ($args.Count -eq 0) {
    Write-Host "Usage: .\idf_with_utf8.ps1 build | .\idf_with_utf8.ps1 -p COM11 flash monitor | etc."
    exit 1
}
& idf.py @args
