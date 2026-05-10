#!/bin/bash
set -e
cmake -B build/win64-release -DCMAKE_BUILD_TYPE=Release --preset win64-release
cmake --build build/win64-release --parallel
