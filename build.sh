#!/bin/sh
# Compila la d3d11.dll con mingw-w64
x86_64-w64-mingw32-gcc -shared -O2 -o d3d11.dll src/d3d11_proxy.c src/d3d11.def -static -static-libgcc -s -ldxguid -luuid -ldxgi
