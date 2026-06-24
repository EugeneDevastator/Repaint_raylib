#!/bin/bash
set -e
cd "$(dirname "$0")"

# fix CMake cache path case (case-insensitive FS like VirtualBox shared folders)
if [ -f build/win64-release/CMakeCache.txt ]; then
    current="$(pwd)"
    cached=$(grep -m1 '^CMAKE_HOME_DIRECTORY' build/win64-release/CMakeCache.txt 2>/dev/null | cut -d= -f2)
    if [ -n "$cached" ] && [ "$cached" != "$current" ] && [ "${cached,,}" = "${current,,}" ]; then
        sed -i "s|$cached|$current|g" build/win64-release/CMakeCache.txt
    fi
fi

cmake -B build/win64-release -DCMAKE_BUILD_TYPE=Release --preset win64-release
cmake --build build/win64-release --parallel
