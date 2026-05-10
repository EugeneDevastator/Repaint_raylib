#!/bin/bash
set -e
cmake -B build/linux-release -DCMAKE_BUILD_TYPE=Release --preset linux-release
cmake --build build/linux-release --parallel
