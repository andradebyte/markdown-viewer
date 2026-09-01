#!/usr/bin/env bash
# Build an RPM for Fedora/RHEL.
# Requires: rpm-build rpmdevtools
#   sudo dnf install rpm-build rpmdevtools
set -euo pipefail

cd "$(dirname "$0")"

VER="1.0.0"
PKG="mdview"

mkdir -p "$HOME/rpmbuild/SOURCES" "$HOME/rpmbuild/SPECS" "$HOME/rpmbuild/BUILD" \
         "$HOME/rpmbuild/RPMS" "$HOME/rpmbuild/SRPMS"

tar czf "$HOME/rpmbuild/SOURCES/${PKG}-${VER}.tar.gz" \
    mdview.c mdview.desktop mdview.svg Makefile LICENSE README.md

cp "${PKG}.spec" "$HOME/rpmbuild/SPECS/"

rpmbuild -ba "$HOME/rpmbuild/SPECS/${PKG}.spec"

echo "RPMs em:"
find "$HOME/rpmbuild/RPMS" -name '*.rpm' -newer "$HOME/rpmbuild/SOURCES/${PKG}-${VER}.tar.gz" -print
