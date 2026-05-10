#!/bin/bash
set -e
cmake -B build/linux-debug -DCMAKE_BUILD_TYPE=Debug --preset linux-debug
cmake --build build/linux-debug --parallel
