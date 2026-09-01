#!/usr/bin/env bash
# Build a .deb package for Debian/Ubuntu.
# Run this ON the target distro (or in a container), because the binary
# must be compiled against that distro's libraries.
#
# Requires (Debian/Ubuntu):
#   sudo apt install debhelper dh-make libgtk-3-dev libwebkit2gtk-4.1-dev
set -euo pipefail

cd "$(dirname "$0")"

rm -rf build
mkdir -p build
cp -r mdview.c mdview.desktop mdview.svg Makefile LICENSE README.md debian build/
cp mdview.desktop build/debian/mdview.desktop

cd build

# Build the actual .deb
dpkg-buildpackage -b -us -uc

echo
echo ".deb gerado:"
ls -1 ../*.deb
