@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

echo ==========================================
echo       T91 雷射定位測試系統 啟動內容
echo ==========================================

echo [1/3] 設定環境變數...
set "PATH=C:\msys64\mingw64\bin;%PATH%"

echo [2/3] 啟動追蹤後端 (t91_tracker.exe)...
taskkill /F /IM t91_tracker.exe /T 2>nul
start "T91 Tracker Backend" cmd /k "cd cpp && .\t91_tracker.exe"

echo [3/3] 開啟測試網頁 (控制台 + 測試網格)...
:: 先開啟管理控制台，用於點擊「重新校準」
start "" "index.html"
:: 再開啟測試網格，用於顯示紅點
start "" "校正測試_v2.html"

echo.
echo 系統已啟動！
echo 請依照「定位測試說明.md」進行後續測試。
echo.
pause
