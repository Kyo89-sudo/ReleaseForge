#!/bin/sh
# Cross-compilazione da Linux con llvm-mingw (usata per verificare e produrre dist/ReleaseForge.exe).
set -e
cd "$(dirname "$0")"
TC=${TC:-/opt/llvm-mingw/bin}
export PATH="$TC:$PATH"
mkdir -p build-mingw dist
x86_64-w64-mingw32-windres -I src src/app/ReleaseForge.rc -O coff -o build-mingw/res.o
x86_64-w64-mingw32-clang++ -std=c++17 -O2 -Wall -Wextra -municode -mwindows -static -s \
  -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
  src/core/*.cpp src/win/*.cpp src/app/main.cpp src/app/dialogs.cpp build-mingw/res.o \
  -lcomctl32 -lwinhttp -lbcrypt -lshlwapi -lshell32 -lole32 -luuid -lgdiplus -luxtheme \
  -o dist/ReleaseForge.exe
echo "OK: dist/ReleaseForge.exe"
