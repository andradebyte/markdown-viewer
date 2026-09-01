#!/usr/bin/env bash
# Build a .deb package WITHOUT dpkg-deb (works on any distro, uses ar + tar).
# NOTE: the binary is compiled on THIS machine, so the .deb matches this
# distro's libc. For a production .deb, run build-deb.sh on Debian/Ubuntu.
set -euo pipefail

cd "$(dirname "$0")/../.."   # project root
# Version comes from $VERSION (e.g. "1.1.1") or the latest git tag, default 1.0.0.
if [[ "${VERSION:-}" =~ ^v?[0-9]+\.[0-9]+\.[0-9]+ ]]; then
    VER="${VERSION#v}"
else
    VER="$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')"
fi
VER="${VER:-1.0.0}"
ARCH="$(dpkg --print-architecture 2>/dev/null || uname -m)"
[ "$ARCH" = "x86_64" ] && ARCH="amd64"
[ "$ARCH" = "aarch64" ] && ARCH="arm64"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo ">> compilando binario..."
make clean >/dev/null 2>&1 || true
make >/dev/null

echo ">> montando data.tar.gz..."
D="$TMP/root"
mkdir -p "$D/usr/bin"
mkdir -p "$D/usr/share/applications"
mkdir -p "$D/usr/share/icons/hicolor/scalable/apps"
mkdir -p "$D/usr/share/doc/mdview"
install -m 0755 mdview "$D/usr/bin/mdview"
sed 's|/home/joaoigorandrade/.local/bin/mdview|/usr/bin/mdview|' mdview.desktop \
    > "$D/usr/share/applications/mdview.desktop"
install -m 0644 mdview.svg "$D/usr/share/icons/hicolor/scalable/apps/mdview.svg"
install -m 0644 LICENSE "$D/usr/share/doc/mdview/copyright"

SIZE_KB=$(du -sk "$D" | cut -f1)

( cd "$D" && tar czf "$TMP/data.tar.gz" usr )

echo ">> montando control.tar.gz..."
C="$TMP/control"
mkdir -p "$C"
cat > "$C/control" <<EOF
Package: mdview
Version: ${VER}-1
Architecture: ${ARCH}
Maintainer: joaoigorandrade
Installed-Size: ${SIZE_KB}
Depends: libgtk-3-0, libwebkit2gtk-4.1-0
Section: utils
Priority: optional
Homepage: https://github.com/joaoigorandrade/mdview
Description: Lightweight Markdown viewer/editor
 A Markdown and text viewer/editor for Linux written in C with GTK3 and
 WebKitGTK. Rendered preview, inline editing, tabs, dark theme, zoom and
 session cache in a single native binary.
EOF
( cd "$D" && find usr -type f -exec md5sum {} \; ) > "$C/md5sums"
( cd "$C" && tar czf "$TMP/control.tar.gz" control md5sums )

echo ">> montando .deb..."
printf '2.0\n' > "$TMP/debian-binary"
OUT="mdview_${VER}-1_${ARCH}.deb"
ar rcs "$OUT" "$TMP/debian-binary" "$TMP/control.tar.gz" "$TMP/data.tar.gz"

echo ">> gerado: $OUT"
