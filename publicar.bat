@echo off
setlocal
cd /d "%~dp0"

set "VERSION=%~1"
if "%VERSION%"=="" set /p "VERSION=Versao da release (ex: 1.0.2): "
if "%VERSION%"=="" exit /b 1

set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if exist "%VCVARS%" (
    call "%VCVARS%" >nul 2>nul
    call "%~dp0build_msvc.bat" %VERSION% || (pause & exit /b 1)
) else (
    set "WINLIBS=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
    call set "PATH=%%WINLIBS%%;%%PATH%%"
    call "%~dp0build.bat" %VERSION% || (pause & exit /b 1)
)

if not exist dist mkdir dist
move /y PrecisionFix.exe dist\PrecisionFix.exe >nul

set "GH=gh"
where gh >nul 2>nul || set "GH=%ProgramFiles%\GitHub CLI\gh.exe"
"%GH%" release create v%VERSION% dist\PrecisionFix.exe --repo orenandev/PrecisionFix --title "Precision Fix v%VERSION%" --generate-notes --latest && (
    echo.
    echo Release v%VERSION% publicada.
    pause
    exit /b 0
)

echo.
echo Nao foi possivel publicar automaticamente. Publique manualmente:
echo   1. Confira a tag v%VERSION% na pagina que vai abrir
echo   2. Arraste dist\PrecisionFix.exe para os anexos
echo   3. Clique em "Publish release"
start "" "https://github.com/orenandev/PrecisionFix/releases/new?tag=v%VERSION%&title=Precision+Fix+v%VERSION%"
explorer /select,"%cd%\dist\PrecisionFix.exe"
pause
