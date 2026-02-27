@echo off
set "CXX=C:/msys64/mingw64/bin/g++"
set "PATH=C:\msys64\mingw64\bin;%PATH%"

echo Testing HitDetectionService compilation...
%CXX% -std=c++17 -Wall -I./include -I"C:/msys64/mingw64/include/opencv4" -c src/HitDetectionService.cpp -o src/HitDetectionService.o 2>&1

echo.
echo Testing CalibrationService compilation...
%CXX% -std=c++17 -Wall -I./include -I"C:/msys64/mingw64/include/opencv4" -c src/CalibrationService.cpp -o src/CalibrationService.o 2>&1

echo.
echo Done!
pause
