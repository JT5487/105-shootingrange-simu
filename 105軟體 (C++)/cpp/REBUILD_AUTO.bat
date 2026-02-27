@echo off
set "CXX=C:/msys64/mingw64/bin/g++"
set "PYLON_DIR=C:/PROGRA~1/Basler/pylon/Development"
echo [1/4] Compiling main.cpp...
%CXX% -std=c++17 -O3 -Wall -D_WIN32_WINNT=0x0A00 -DPYLON_WIN_BUILD -DPYLON_64_BUILD -I./include -I"C:/msys64/mingw64/include/opencv4" -I"%PYLON_DIR%/include" -c src/main.cpp -o src/main.o
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo [2/4] Compiling IRTracker.cpp...
%CXX% -std=c++17 -O3 -Wall -D_WIN32_WINNT=0x0A00 -DPYLON_WIN_BUILD -DPYLON_64_BUILD -I./include -I"C:/msys64/mingw64/include/opencv4" -I"%PYLON_DIR%/include" -c src/IRTracker.cpp -o src/IRTracker.o
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo [3/4] Compiling CameraManager.cpp...
%CXX% -std=c++17 -O3 -Wall -D_WIN32_WINNT=0x0A00 -DPYLON_WIN_BUILD -DPYLON_64_BUILD -I./include -I"C:/msys64/mingw64/include/opencv4" -I"%PYLON_DIR%/include" -c src/CameraManager.cpp -o src/CameraManager.o
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo [4/4] Linking t91_tracker.exe...
%CXX% -o t91_tracker.exe src/main.o src/IRTracker.o src/CameraManager.o -L"C:/msys64/mingw64/lib" -lopencv_core -lopencv_imgproc -lopencv_highgui -lopencv_videoio -lopencv_calib3d -lopencv_imgcodecs -L"%PYLON_DIR%/lib/x64" -lPylonC_v10 -lPylonBase_v11 -lPylonUtility_v11 -lGCBase_MD_VC142_v3_5_Basler_pylon_v1 -lGenApi_MD_VC142_v3_5_Basler_pylon_v1 -lws2_32 -lcrypt32 -liphlpapi -lpthread -lstdc++
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%

echo Build Successful.
