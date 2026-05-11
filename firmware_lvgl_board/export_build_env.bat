@echo off
REM Fix kconfgen UnicodeDecodeError on Chinese Windows before idf.py
set PYTHONUTF8=1
set PYTHONIOENCODING=utf-8
echo PYTHONUTF8=1 set. Run idf.py in this cmd window.
