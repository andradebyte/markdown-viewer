#!/usr/bin/env bash
# Build an RPM for Fedora/RHEL.
# Requires: rpm-build  (sudo dnf install rpm-build)
# Run from anywhere; it locates the project root itself.
# Version comes from $VERSION (e.g. "1.1.1") or the latest git tag, default 1.0.0.
set -euo pipefail

cd "$(dirname "$0")/../.."   # project root

PKG="mdview"
if [[ "${VERSION:-}" =~ ^v?[0-9]+\.[0-9]+\.[0-9]+ ]]; then
    VER="${VERSION#v}"
else
    VER="$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//')"
fi
VER="${VER:-1.0.0}"

mkdir -p "$HOME/rpmbuild/SOURCES" "$HOME/rpmbuild/SPECS" "$HOME/rpmbuild/BUILD" \
         "$HOME/rpmbuild/RPMS" "$HOME/rpmbuild/SRPMS"

# rpmbuild's %setup expects a top-level directory inside the tarball.
STAGE=$(mktemp -d)
mkdir -p "$STAGE/${PKG}-${VER}"
cp mdview.c mdview.desktop mdview.svg Makefile LICENSE README.md "$STAGE/${PKG}-${VER}/"
tar czf "$HOME/rpmbuild/SOURCES/${PKG}-${VER}.tar.gz" -C "$STAGE" "${PKG}-${VER}"
rm -rf "$STAGE"

cp "packaging/fedora/${PKG}.spec" "$HOME/rpmbuild/SPECS/"

rpmbuild -ba --define "mdview_version ${VER}" "$HOME/rpmbuild/SPECS/${PKG}.spec"

# Copy the binary RPM into the current dir so CI can pick it up as an artifact.
cp "$HOME"/rpmbuild/RPMS/*/*.rpm .
echo "RPMs (versão $VER):"
ls -1 *.rpm
