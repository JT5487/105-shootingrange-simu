@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo [Remote Mode] Starting T91 System...
powershell -ExecutionPolicy Bypass -File "啟動系統_遠端模式.ps1"
echo.
pause
