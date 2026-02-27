@echo off
echo ========================================
echo T91 系統編譯 - 含新功能 A1 + B4
echo ========================================
echo.

set "CXX=C:/msys64/mingw64/bin/g++"
set "PYLON_DIR=C:/PROGRA~1/Basler/pylon/Development"
set "PATH=C:\msys64\mingw64\bin;%PATH%"

echo [1/6] 編譯 main.cpp...
%CXX% -std=c++17 -O3 -Wall -DUSE_PYLON -D_WIN32_WINNT=0x0A00 -DPYLON_WIN_BUILD -DPYLON_64_BUILD -I./include -I"C:/msys64/mingw64/include/opencv4" -I"%PYLON_DIR%/include" -c src/main.cpp -o src/main.o

if errorlevel 1 (
    echo 錯誤：main.cpp 編譯失敗
    pause
    exit /b 1
)

echo [2/6] 編譯 HitDetectionService.cpp...
%CXX% -std=c++17 -O3 -Wall -I./include -I"C:/msys64/mingw64/include/opencv4" -c src/HitDetectionService.cpp -o src/HitDetectionService.o

if errorlevel 1 (
    echo 錯誤：HitDetectionService.cpp 編譯失敗
    pause
    exit /b 1
)

echo [3/6] 編譯 CalibrationService.cpp...
%CXX% -std=c++17 -O3 -Wall -I./include -I"C:/msys64/mingw64/include/opencv4" -c src/CalibrationService.cpp -o src/CalibrationService.o

if errorlevel 1 (
    echo 錯誤：CalibrationService.cpp 編譯失敗
    pause
    exit /b 1
)

echo [4/6] 編譯 IRTracker.cpp...
%CXX% -std=c++17 -O3 -Wall -DUSE_PYLON -D_WIN32_WINNT=0x0A00 -DPYLON_WIN_BUILD -DPYLON_64_BUILD -I./include -I"C:/msys64/mingw64/include/opencv4" -I"%PYLON_DIR%/include" -c src/IRTracker.cpp -o src/IRTracker.o

if errorlevel 1 (
    echo 錯誤：IRTracker.cpp 編譯失敗
    pause
    exit /b 1
)

echo [5/6] 編譯 CameraManager.cpp...
%CXX% -std=c++17 -O3 -Wall -DUSE_PYLON -D_WIN32_WINNT=0x0A00 -DPYLON_WIN_BUILD -DPYLON_64_BUILD -I./include -I"C:/msys64/mingw64/include/opencv4" -I"%PYLON_DIR%/include" -c src/CameraManager.cpp -o src/CameraManager.o

if errorlevel 1 (
    echo 錯誤：CameraManager.cpp 編譯失敗
    pause
    exit /b 1
)

echo [6/6] 連結 t91_tracker.exe...
%CXX% -o t91_tracker.exe src/main.o src/IRTracker.o src/CameraManager.o src/HitDetectionService.o src/CalibrationService.o -L"C:/msys64/mingw64/lib" -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_videoio -lopencv_calib3d -lopencv_imgcodecs -L"%PYLON_DIR%/lib/x64" -lPylonC_v10 -lPylonBase_v11 -lPylonUtility_v11 -lGCBase_MD_VC142_v3_5_Basler_pylon_v1 -lGenApi_MD_VC142_v3_5_Basler_pylon_v1 -lws2_32 -lcrypt32 -liphlpapi -lpthread -lstdc++

if errorlevel 1 (
    echo 錯誤：連結失敗
    pause
    exit /b 1
)

echo.
echo ========================================
echo 編譯成功！t91_tracker.exe 已生成
echo ========================================
echo.
pause
