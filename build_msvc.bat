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

rc /nologo /I res /I build /fo build\app.res res\app.rc || goto :fail

cl /nologo /O2 /MT /EHsc /std:c++17 /utf-8 /W3 /GS /guard:cf ^
   /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /D_CRT_SECURE_NO_WARNINGS /DAPP_VERSION=\"%VERSION%\" ^
   src\main.cpp src\tweaks.cpp src\sysinfo.cpp src\extras.cpp src\net.cpp src\updater.cpp build\app.res ^
   /Fobuild\ /FePrecisionFix.exe ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /DYNAMICBASE /NXCOMPAT /guard:cf ^
   gdiplus.lib dwmapi.lib powrprof.lib gdi32.lib user32.lib advapi32.lib shell32.lib ole32.lib ws2_32.lib winhttp.lib comdlg32.lib || goto :fail

echo.
echo OK: PrecisionFix.exe v%VERSION% (MSVC)
exit /b 0

:fail
echo.
echo ERRO na compilacao.
exit /b 1
