# Markdown Viewer

A lightweight Markdown and text viewer/editor for Linux, written in C with GTK3 and WebKitGTK. Open `.md` files with a double-click and get a rendered preview, inline editing, tabs, themes and zoom — no Electron, no JavaScript, just one small native binary.

## Features

- **Rendered Markdown preview** — GitHub-like styling, light and dark themes
- **Inline editing** — switch between raw source and preview with one button
- **Tabs** — open multiple files (Ctrl+T new tab, Ctrl+W close tab)
- **Session cache** — open files and even unsaved edits are restored on next launch (`~/.cache/mdview/`)
- **Zoom** — Ctrl+scroll (50%–400%), Ctrl++ / Ctrl+- / Ctrl+0
- **Copy** — one click copies the file content (or rendered HTML preview)
- **Auto-reload** — the preview refreshes when the file changes on disk
- **Internal links** — click links to other `.md`/`.txt` files to open them in a new tab; `http(s)` links open in your browser

## Shortcuts

| Shortcut | Action |
|----------|--------|
| `Preview` / `Edit` button | Toggle rendered preview / raw source |
| `Ctrl+S` | Save |
| `Ctrl+E` | Toggle preview |
| `Ctrl+O` | Open file (new tab) |
| `Ctrl+T` | New tab |
| `Ctrl+W` | Close tab |
| `Ctrl+C` | Copy content |
| `Ctrl+scroll` / `Ctrl+±` / `Ctrl+0` | Zoom in/out / reset |

## Install

### Dependencies (Fedora)

```bash
sudo dnf install gcc pkg-config gtk3-devel webkit2gtk4.1-devel
```

### Build

```bash
make
```

or manually:

```bash
gcc -o mdview mdview.c $(pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.1) -O2
```

### Install (user-wide)

```bash
mkdir -p ~/.local/bin \
         ~/.local/share/applications \
         ~/.local/share/icons/hicolor/scalable/apps

cp mdview ~/.local/bin/
cp mdview.desktop ~/.local/share/applications/
cp mdview.svg  ~/.local/share/icons/hicolor/scalable/apps/

update-desktop-database ~/.local/share/applications
gtk-update-icon-cache -f ~/.local/share/icons/hicolor

# Associate .md/.txt with this app
xdg-mime default mdview.desktop text/markdown text/x-markdown text/plain
```

Double-clicking any `.md` (or `.txt`) file in your file manager now opens it in the Markdown Viewer.

## Usage

- Open a file: `mdview file.md` or double-click it
- The file opens in **edit mode** (raw text); click **Preview** to see it rendered
- New tabs (Ctrl+T) open blank and are kept in the session cache until you save them as files (Ctrl+S → Save As)

## How it works

`mdview.c` is a single-file application:

1. A small **Markdown → HTML** parser written from scratch (headings, emphasis, code blocks, lists, blockquotes, tables, links, images, horizontal rules).
2. A **WebKitGTK** web view renders the HTML with embedded CSS (light/dark).
3. A **GtkTextView** editor for the raw source; a `GtkStack` swaps between the two.
4. A **GtkNotebook** provides tabs; a session file in `~/.cache/mdview/` persists open files and untitled content.

## Project layout

```
mdview.c       — source (single file, Linux)
mdview.desktop — desktop entry (file association)
mdview.svg     — application icon
test_md.c      — small markdown parser test harness
packaging/     — Fedora (.spec), Debian/Ubuntu (.deb) build scripts
windows/       — Windows build (Win32 + WebView2), shared parser
```

## Building for other platforms

- **Fedora / RHEL** (RPM): `packaging/fedora/build-rpm.sh` (needs `rpm-build`)
- **Debian / Ubuntu** (.deb): `packaging/debian/build-deb.sh` on a Debian/Ubuntu box (needs `dh`, `libgtk-3-dev`, `libwebkit2gtk-4.1-dev`), or `build-deb-manual.sh` anywhere (produces a `.deb` for the current distro)
- **Windows**: `windows/README.md` (Win32 + WebView2 + RichEdit port, shared Markdown parser; build with MSYS2/MinGW)

## Requirements

- GTK 3
- WebKitGTK 4.1 (`webkit2gtk-4.1`)
- GLib

## License

MIT
