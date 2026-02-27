@echo off
chcp 65001 >nul
echo ============================================
echo   T91 Tracker - Rebuild Script (Fixed)
echo ============================================

REM Set up environment
set "PATH=C:\msys64\mingw64\bin;%PATH%"
set "CXX=g++"
set "PYLON_DIR=C:\Program Files\Basler\pylon\Development"

REM Navigate to cpp directory using virtual drive
cd /d X:\cpp
if errorlevel 1 (
    echo ERROR: Cannot access X:\cpp. Make sure virtual drive is mounted.
    pause
    exit /b 1
)

echo.
echo [1/4] Compiling main.cpp...
%CXX% -std=c++17 -O3 -Wall -D_WIN32_WINNT=0x0A00 -DPYLON_WIN_BUILD -DPYLON_64_BUILD ^
    -I./include ^
    -I"C:/msys64/mingw64/include/opencv4" ^
    -I"%PYLON_DIR%/include" ^
    -c src/main.cpp -o src/main.o
if errorlevel 1 (
    echo ERROR: Failed to compile main.cpp
    pause
    exit /b 1
)

echo [2/4] Compiling IRTracker.cpp...
%CXX% -std=c++17 -O3 -Wall -D_WIN32_WINNT=0x0A00 -DPYLON_WIN_BUILD -DPYLON_64_BUILD ^
    -I./include ^
    -I"C:/msys64/mingw64/include/opencv4" ^
    -I"%PYLON_DIR%/include" ^
    -c src/IRTracker.cpp -o src/IRTracker.o
if errorlevel 1 (
    echo ERROR: Failed to compile IRTracker.cpp
    pause
    exit /b 1
)

echo [3/4] Compiling CameraManager.cpp...
%CXX% -std=c++17 -O3 -Wall -D_WIN32_WINNT=0x0A00 -DPYLON_WIN_BUILD -DPYLON_64_BUILD ^
    -I./include ^
    -I"C:/msys64/mingw64/include/opencv4" ^
    -I"%PYLON_DIR%/include" ^
    -c src/CameraManager.cpp -o src/CameraManager.o
if errorlevel 1 (
    echo ERROR: Failed to compile CameraManager.cpp
    pause
    exit /b 1
)

echo [4/4] Linking t91_tracker.exe...
%CXX% -o t91_tracker.exe ^
    src/main.o src/IRTracker.o src/CameraManager.o ^
    -L"C:/msys64/mingw64/lib" ^
    -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_videoio -lopencv_calib3d -lopencv_imgcodecs ^
    -L"%PYLON_DIR%/lib/x64" ^
    -lPylonC_v10 -lPylonBase_v11 -lPylonUtility_v11 ^
    -lGCBase_MD_VC142_v3_5_Basler_pylon_v1 -lGenApi_MD_VC142_v3_5_Basler_pylon_v1 ^
    -lws2_32 -lcrypt32 -liphlpapi -lpthread -lstdc++
if errorlevel 1 (
    echo ERROR: Failed to link t91_tracker.exe
    pause
    exit /b 1
)

echo.
echo ============================================
echo   BUILD SUCCESSFUL!
echo ============================================
if exist t91_tracker.exe (
    echo   t91_tracker.exe created successfully
    dir t91_tracker.exe
) else (
    echo   WARNING: Executable not found!
)
echo.
pause
