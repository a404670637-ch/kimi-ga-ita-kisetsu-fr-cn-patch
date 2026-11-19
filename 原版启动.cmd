@echo off
rem 原版启动器（不加载汉化）
cd /d "%~dp0"
set CNPATCH_DISABLE=1
start "" "%~dp0Kimiita.exe"
exit /b 0
