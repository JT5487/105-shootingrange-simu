@echo off
echo ========================================
echo T91 射擊追蹤系統 - 編譯腳本
echo ========================================
echo.

cd /d "C:\Users\CMA\Desktop\105軟體 (C++)2026\105軟體 (C++)\cpp"

if not exist "REBUILD.bat" (
    echo 錯誤：找不到 REBUILD.bat
    pause
    exit /b 1
)

echo 開始編譯...
echo.

call REBUILD.bat

echo.
echo ========================================
echo 編譯完成！
echo ========================================
pause
