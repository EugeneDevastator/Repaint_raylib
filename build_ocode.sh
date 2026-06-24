#!/bin/sh
# First build — run from project root:
#   mkdir -p build/ocode-linux
#   cmake -B build/ocode-linux -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/ocode-linux -j"$(nproc)"
#
# Incremental rebuild:
set -e
cd "$(dirname "$0")"
cmake --build build/ocode-linux -j"$(nproc)" 2>&1 | tail -10
echo "---"
echo "Binary: build/ocode-linux/bin/repaint"
