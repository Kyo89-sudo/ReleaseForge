#!/bin/sh
# Cross-compilazione da Linux con llvm-mingw (usata per verificare e produrre dist/ReleaseForge.exe).
set -e
cd "$(dirname "$0")"
TC=${TC:-/opt/llvm-mingw/bin}
export PATH="$TC:$PATH"
mkdir -p build-mingw dist
x86_64-w64-mingw32-windres -I src src/app/ReleaseForge.rc -O coff -o build-mingw/res.o
x86_64-w64-mingw32-clang++ -std=c++23 -O2 -Wall -Wextra -municode -mwindows -static -s \
  -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
  src/core/*.cpp src/win/*.cpp src/app/main.cpp src/app/dialogs.cpp build-mingw/res.o \
  -lcomctl32 -lwinhttp -lbcrypt -lshlwapi -lshell32 -lole32 -luuid -lgdiplus -luxtheme \
  -o dist/ReleaseForge.exe
echo "OK: dist/ReleaseForge.exe"

# Installer nativo: prima uninstall.exe (senza payload), poi il setup che lo incorpora.
VER=$(sed -n 's/^#define RF_VERSION_STR "\(.*\)"/\1/p' src/resource.h)
mkdir -p build-mingw/payload
INST_FLAGS="-std=c++23 -O2 -Wall -Wextra -municode -mwindows -static -s -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00"
INST_LIBS="-lcomctl32 -lshell32 -lole32 -luuid -ladvapi32"
x86_64-w64-mingw32-windres installer/installer.rc -O coff -o build-mingw/uninst-res.o
x86_64-w64-mingw32-clang++ $INST_FLAGS installer/setup.cpp build-mingw/uninst-res.o $INST_LIBS -o build-mingw/payload/uninstall.exe
x86_64-w64-mingw32-windres -DRF_WITH_PAYLOAD -I dist -I build-mingw/payload installer/installer.rc -O coff -o build-mingw/setup-res.o
x86_64-w64-mingw32-clang++ $INST_FLAGS installer/setup.cpp build-mingw/setup-res.o $INST_LIBS -o "dist/ReleaseForge-$VER-win64-setup.exe"
echo "OK: dist/ReleaseForge-$VER-win64-setup.exe"
