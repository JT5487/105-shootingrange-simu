@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo 正在啟動自動化腳本...
powershell -ExecutionPolicy Bypass -File "啟動系統_自動化.ps1"
echo.
echo 如果看到錯誤訊息，請截圖告訴我
pause
