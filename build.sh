#!/bin/bash
set -e
cd "$(dirname "$0")"

# fix CMake cache path case (case-insensitive FS like VirtualBox shared folders)
if [ -f build/ocode-linux-deb/CMakeCache.txt ]; then
    current="$(pwd)"
    cached=$(grep -m1 '^CMAKE_HOME_DIRECTORY' build/ocode-linux-deb/CMakeCache.txt 2>/dev/null | cut -d= -f2)
    if [ -n "$cached" ] && [ "$cached" != "$current" ] && [ "${cached,,}" = "${current,,}" ]; then
        sed -i "s|$cached|$current|g" build/ocode-linux-deb/CMakeCache.txt
    fi
fi

cmake -B build/ocode-linux-deb -DCMAKE_BUILD_TYPE=Debug --preset linux-debug
cmake --build build/ocode-linux-deb --parallel
