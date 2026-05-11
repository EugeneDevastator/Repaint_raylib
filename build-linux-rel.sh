#!/bin/bash
set -e
cd "$(dirname "$0")"

# fix CMake cache path case (case-insensitive FS like VirtualBox shared folders)
if [ -f build/linux-release/CMakeCache.txt ]; then
    current="$(pwd)"
    cached=$(grep -m1 '^CMAKE_HOME_DIRECTORY' build/linux-release/CMakeCache.txt 2>/dev/null | cut -d= -f2)
    if [ -n "$cached" ] && [ "$cached" != "$current" ] && [ "${cached,,}" = "${current,,}" ]; then
        sed -i "s|$cached|$current|g" build/linux-release/CMakeCache.txt
    fi
fi

cmake -B build/linux-release -DCMAKE_BUILD_TYPE=Release --preset linux-release
cmake --build build/linux-release --parallel
