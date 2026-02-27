@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

echo [1/3] Creating system path for dependencies...
set "PATH=C:\msys64\mingw64\bin;%PATH%"

echo [2/3] Starting T91 Tracker Backend...
taskkill /F /IM t91_tracker.exe /T 2>nul
start "T91 Tracker Backend" cmd /k "cd cpp && .\t91_tracker.exe"

REM 等待後端啟動
timeout /t 3 /nobreak >nul

echo [3/3] Launching screens...

REM 螢幕 1：射擊訓練控制台 (index.html)
start "" "%~dp0index.html"

REM 等待第一個視窗開啟
timeout /t 1 /nobreak >nul

REM 螢幕 2：左投影幕 (training_screen_a.html) - 區域 1-2-3
start "" "%~dp0training_screen_a.html"

REM 等待第二個視窗開啟
timeout /t 1 /nobreak >nul

REM 螢幕 3：右投影幕 (training_screen_b.html) - 區域 4-5-6
start "" "%~dp0training_screen_b.html"

echo.
echo ==========================================
echo       T91 System Started Successfully!
echo ==========================================
echo.
echo 操作步驟：
echo 1. 將各視窗拖到對應螢幕
echo 2. 按 F11 進入全螢幕
echo    - 螢幕1: 控制台
echo    - 螢幕2: 左投影幕 (1-2-3)
echo    - 螢幕3: 右投影幕 (4-5-6)
echo.
pause

