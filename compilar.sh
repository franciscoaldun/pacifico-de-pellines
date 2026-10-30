#!/bin/sh
# Compila Pacífico de Pellines para Windows de 64 bits con MinGW-w64 (desde Linux, WSL o MSYS2).
# Queda PacificoDePellines.exe en esta carpeta.
set -e
cd "$(dirname "$0")/src"
x86_64-w64-mingw32-windres recursos.rc -O coff -o recursos.o
x86_64-w64-mingw32-gcc -O3 -mavx2 -mfma -mf16c -fno-math-errno -mwindows -static -s pacifico.c recursos.o \
    -o ../PacificoDePellines.exe -lole32 -lavrt -ldwmapi -lgdi32 -luser32 -lwinmm -lwinhttp -ld3d11 -ldxgi -ldxguid -lm
rm -f recursos.o
echo "listo: PacificoDePellines.exe"
