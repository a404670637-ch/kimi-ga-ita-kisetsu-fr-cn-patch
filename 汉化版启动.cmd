@echo off
rem 汉化补丁启动器（中文）
cd /d "%~dp0"
if not exist "%~dp0SHFolder.dll" goto nohook
start "" "%~dp0Kimiita.exe"
exit /b 0
:nohook
echo [错误] 找不到 SHFolder.dll（补丁文件不完整）。请把补丁文件放回游戏目录。
pause
exit /b 1
