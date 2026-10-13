@echo off
setlocal
cd /d "%~dp0"

set "TAG=v2"
for /f %%d in ('powershell -NoProfile -Command "Get-Date -Format yyyy.M.d"') do set "VERSION=%%d"

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
set "REPO=orenandev/PrecisionFix"

"%GH%" release view %TAG% --repo %REPO% >nul 2>nul
if errorlevel 1 (
    "%GH%" release create %TAG% dist\PrecisionFix.exe --repo %REPO% --title "Precision Fix" --notes "Baixe o PrecisionFix.exe e execute como administrador. O app se atualiza sozinho ao abrir." --latest || goto :fail
) else (
    "%GH%" release upload %TAG% dist\PrecisionFix.exe --repo %REPO% --clobber || goto :fail
    "%GH%" release edit %TAG% --repo %REPO% --latest >nul
)
echo.
echo PrecisionFix.exe atualizado na release. Quem abrir o app recebe a nova versao.
pause
exit /b 0

:fail
echo.
echo Nao foi possivel publicar. Confira se o GitHub CLI esta logado (gh auth login).
pause
exit /b 1
