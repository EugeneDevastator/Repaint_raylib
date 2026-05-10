#!/bin/bash
set -e
cmake -B build/win64-debug -DCMAKE_BUILD_TYPE=Debug --preset win64-debug
cmake --build build/win64-debug --parallel
