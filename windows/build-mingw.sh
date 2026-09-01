#!/usr/bin/env bash
# Build mdview.exe for Windows (MinGW-w64, run inside an MSYS2 MinGW64 terminal).
#
# Setup once:
#   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf \
#             mingw-w64-x86_64-glib2 mingw-w64-x86_64-winpthreads
#   powershell -ExecutionPolicy Bypass -File download-webview2.ps1
set -euo pipefail
cd "$(dirname "$0")"

gcc -O2 -mwindows -municode -o mdview.exe mdview.c \
    $(pkg-config --cflags --libs glib-2.0) \
    -lcomctl32 -lcomdlg32 -lole32 -loleaut32 -luuid -lshlwapi -lshell32 -lgdi32 -luser32

if [ ! -f WebView2Loader.dll ]; then
    echo "AVISO: WebView2Loader.dll nao encontrado - rode download-webview2.ps1" >&2
fi

echo "OK: mdview.exe gerado (WebView2Loader.dll precisa ficar ao lado dele)"
