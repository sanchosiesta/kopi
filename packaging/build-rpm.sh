#!/usr/bin/env bash
#
# Build an installable kopi RPM for Fedora / RHEL / Rocky.
#
# Needs the build dependencies installed once:
#   sudo dnf install gcc-c++ cmake make rpm-build SDL3-devel libXtst-devel libX11-devel
#
# Run from anywhere; produces the RPM under ~/rpmbuild/RPMS/<arch>/.
set -euo pipefail

VERSION=1.0.0
NAME=kopi
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "==> Setting up the rpmbuild tree"
mkdir -p "$HOME/rpmbuild"/{BUILD,BUILDROOT,RPMS,SOURCES,SPECS,SRPMS}

echo "==> Packaging source tarball from git HEAD"
git -C "$ROOT" archive --format=tar.gz \
    -o "$HOME/rpmbuild/SOURCES/$NAME-$VERSION.tar.gz" HEAD

echo "==> Copying the spec into place"
cp "$ROOT/packaging/$NAME.spec" "$HOME/rpmbuild/SPECS/"

echo "==> Building the RPM"
rpmbuild -ba "$HOME/rpmbuild/SPECS/$NAME.spec"

echo "==> Built:"
ls -lh "$HOME/rpmbuild/RPMS/"*/*.rpm
