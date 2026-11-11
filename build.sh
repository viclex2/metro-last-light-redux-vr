#!/bin/sh
# Compila la d3d11.dll con mingw-w64 (+ MinHook, licencia BSD, en src/minhook)
MH=src/minhook/src
x86_64-w64-mingw32-gcc -shared -O2 -o d3d11.dll src/d3d11_proxy.c \
  $MH/hook.c $MH/buffer.c $MH/trampoline.c $MH/hde/hde64.c \
  -Isrc/minhook/include src/d3d11.def -static -static-libgcc -s -ldxguid -luuid -ldxgi
