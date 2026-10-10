#!/usr/bin/env bash
# Compila o PrecisionFix.exe com MinGW-w64 (MSYS2 UCRT64 ou Git Bash com MinGW no PATH)
# Uso: ./build.sh [versão]      ex: ./build.sh 1.0.12
set -euo pipefail
cd "$(dirname "$0")"

VERSION="${1:-1.0.0}"
IFS=. read -r MAJOR MINOR PATCH <<< "$VERSION"
mkdir -p build
cat > build/version.h <<VER
#define VER_MAJOR $MAJOR
#define VER_MINOR $MINOR
#define VER_PATCH $PATCH
#define VER_STRING "$VERSION"
VER

windres -I res -I build res/app.rc -O coff -o build/app.res.o

# substitui o manifesto padrão do MinGW (asInvoker) por um objeto vazio — o nosso pede administrador
echo "" > build/empty.c
gcc -c build/empty.c -o build/default-manifest.o

g++ -B build/ -O2 -std=c++17 -municode -mwindows -static -s \
  -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DAPP_VERSION="\"$VERSION\"" \
  src/main.cpp src/tweaks.cpp src/sysinfo.cpp src/extras.cpp src/net.cpp src/updater.cpp build/app.res.o \
  -o PrecisionFix.exe \
  -lgdiplus -ldwmapi -lpowrprof -lgdi32 -luser32 -ladvapi32 -lshell32 -lole32 -lws2_32 -lwinhttp

echo "OK: PrecisionFix.exe v$VERSION"
