@echo off
setlocal
cd /d "%~dp0"
set VERSION=%1
if "%VERSION%"=="" set VERSION=1.0.0
for /f "tokens=1-3 delims=." %%a in ("%VERSION%") do (set MAJOR=%%a& set MINOR=%%b& set PATCH=%%c)
if not exist build mkdir build
> build\version.h echo #define VER_MAJOR %MAJOR%
>> build\version.h echo #define VER_MINOR %MINOR%
>> build\version.h echo #define VER_PATCH %PATCH%
>> build\version.h echo #define VER_STRING "%VERSION%"

windres -I res -I build res\app.rc -O coff -o build\app.res.o || goto :fail

echo.> build\empty.c
gcc -c build\empty.c -o build\default-manifest.o || goto :fail

g++ -B build\ -O2 -std=c++17 -municode -mwindows -static -s ^
  -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DAPP_VERSION=\"%VERSION%\" ^
  src\main.cpp src\tweaks.cpp src\sysinfo.cpp src\extras.cpp src\net.cpp src\updater.cpp build\app.res.o ^
  -o PrecisionFix.exe ^
  -lgdiplus -ldwmapi -lpowrprof -lgdi32 -luser32 -ladvapi32 -lshell32 -lole32 -lws2_32 -lwinhttp -lcomdlg32 || goto :fail

echo.
echo OK: PrecisionFix.exe v%VERSION%
exit /b 0

:fail
echo.
echo ERRO na compilacao.
exit /b 1
