# mdview — Windows build

Windows port of the Markdown viewer, written in C using **Win32 + WebView2 (Chromium) + RichEdit**.

- The Markdown parser (`parser.c`) is shared with the Linux build.
- Editing uses a native RichEdit control.
- The rendered preview uses **Microsoft Edge WebView2** (Chromium), preinstalled on Windows 11 and most Windows 10 systems with Edge.
- Dark theme by default, zoom with `Ctrl+scroll`, session cache in `%LOCALAPPDATA%\mdview\`.

## Requirements

- **MSYS2** with the MinGW-w64 toolchain (or any MinGW-w64 gcc)
- **WebView2 SDK** files: `WebView2.h` + `WebView2Loader.dll`
- **GLib 2** (for the shared parser)
- **WebView2 Runtime** (comes with Windows 11 / Edge)

## Build (MSYS2)

Open the **MSYS2 MinGW64** terminal:

```bash
# 1. toolchain + glib
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf \
          mingw-w64-x86_64-glib2 mingw-w64-x86_64-winpthreads

# 2. WebView2 SDK (header + loader DLL)
powershell -ExecutionPolicy Bypass -File download-webview2.ps1

# 3. compile
./build-mingw.sh
```

This produces `mdview.exe`. `WebView2Loader.dll` must sit next to the exe.

## Running

```powershell
.\mdview.exe arquivo.md
```

or double-click a `.md`/`.txt` file (set it as the default app the first time).

## Shortcuts

| Shortcut | Action |
|----------|--------|
| `Preview` / `Edit` button | Toggle rendered preview / raw source |
| `Ctrl+S` | Save (or Save As for untitled) |
| `Ctrl+E` | Toggle preview |
| `Ctrl+O` | Open file (new tab) |
| `Ctrl+T` | New tab |
| `Ctrl+W` | Close tab |
| `Ctrl+C` | Copy content (selection when editing) |
| `Ctrl+scroll` / `Ctrl+±` / `Ctrl+0` | Zoom in/out / reset |

## Notes

- Only the **dark** theme is implemented on Windows.
- The preview is written to a temporary HTML file (with a `<base>` pointing to the source folder), so images and relative links work.
- Clicking a link to another `.md`/`.txt` opens it in a new tab; `http(s)` links open in your browser.
- Unsaved edits and untitled tabs are restored on next launch via `%LOCALAPPDATA%\mdview\`.
