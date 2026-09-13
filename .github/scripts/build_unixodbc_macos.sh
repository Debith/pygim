#!/usr/bin/env bash
# Build unixODBC for the macOS wheels (cibuildwheel's before-all).
#
# The persistence extensions link libodbc, and delocate copies it into the
# wheel. Homebrew's bottle targets the runner's own macOS release, newer than
# the wheels' MACOSX_DEPLOYMENT_TARGET, which delocate refuses; so build it
# from source for the wheels' target. Installed under /usr/local, where
# setup.py looks for sql.h and the library on macOS.
set -euo pipefail

VERSION=2.3.14
SHA256=4e2814de3e01fc30b0b9f75e83bb5aba91ab0384ee951286504bb70205524771
: "${MACOSX_DEPLOYMENT_TARGET:?must be the deployment target of the wheels}"

# setup.py searches /opt/homebrew before /usr/local, so a preinstalled
# Homebrew unixODBC would be linked instead of this one. Unlink it rather than
# uninstall it: that removes it from /opt/homebrew/{include,lib} but keeps
# /opt/homebrew/opt/unixodbc, which pyodbc's wheel (a test dependency) loads.
brew unlink unixodbc 2>/dev/null || true

work=$(mktemp -d)
cd "$work"
curl -fsSL -o unixODBC.tar.gz "https://www.unixodbc.org/unixODBC-${VERSION}.tar.gz"
echo "${SHA256}  unixODBC.tar.gz" | shasum -a 256 -c -
tar xzf unixODBC.tar.gz
cd "unixODBC-${VERSION}"

# The bundled libltdl, not a Homebrew one (which targets the runner's macOS);
# no readline or iconv from Homebrew for the same reason.
./configure --prefix=/usr/local --with-included-ltdl --enable-readline=no --enable-iconv=no \
    CFLAGS="-O2 -mmacosx-version-min=${MACOSX_DEPLOYMENT_TARGET}"
make -j"$(sysctl -n hw.ncpu)"
sudo make install
