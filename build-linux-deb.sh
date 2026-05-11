#!/bin/bash
set -e
cd "$(dirname "$0")"

# fix CMake cache path case (case-insensitive FS like VirtualBox shared folders)
if [ -f build/linux-debug/CMakeCache.txt ]; then
    current="$(pwd)"
    cached=$(grep -m1 '^CMAKE_HOME_DIRECTORY' build/linux-debug/CMakeCache.txt 2>/dev/null | cut -d= -f2)
    if [ -n "$cached" ] && [ "$cached" != "$current" ] && [ "${cached,,}" = "${current,,}" ]; then
        sed -i "s|$cached|$current|g" build/linux-debug/CMakeCache.txt
    fi
fi

cmake -B build/linux-debug -DCMAKE_BUILD_TYPE=Debug --preset linux-debug
cmake --build build/linux-debug --parallel
