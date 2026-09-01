#!/usr/bin/env bash
# Build an RPM for Fedora/RHEL.
# Requires: rpm-build  (sudo dnf install rpm-build)
# Run from anywhere; it locates the project root itself.
set -euo pipefail

cd "$(dirname "$0")/../.."   # project root

VER="1.0.0"
PKG="mdview"

mkdir -p "$HOME/rpmbuild/SOURCES" "$HOME/rpmbuild/SPECS" "$HOME/rpmbuild/BUILD" \
         "$HOME/rpmbuild/RPMS" "$HOME/rpmbuild/SRPMS"

# rpmbuild's %setup expects a top-level directory inside the tarball.
STAGE=$(mktemp -d)
mkdir -p "$STAGE/${PKG}-${VER}"
cp mdview.c mdview.desktop mdview.svg Makefile LICENSE README.md "$STAGE/${PKG}-${VER}/"
tar czf "$HOME/rpmbuild/SOURCES/${PKG}-${VER}.tar.gz" -C "$STAGE" "${PKG}-${VER}"
rm -rf "$STAGE"

cp "packaging/fedora/${PKG}.spec" "$HOME/rpmbuild/SPECS/"

rpmbuild -ba "$HOME/rpmbuild/SPECS/${PKG}.spec"

# Copy the binary RPM into the current dir so CI can pick it up as an artifact.
cp "$HOME"/rpmbuild/RPMS/*/*.rpm .
echo "RPMs:"
ls -1 *.rpm
