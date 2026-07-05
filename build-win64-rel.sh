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

# ── Build nnserver standalone ──────────────────────────────────────────────
echo "=== Building nnserver (standalone) ==="
cd NNModelServer
cmake -B ../build/nnserver -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=mingw32-make
cmake --build ../build/nnserver --parallel
cd ..

# ── Package nnserver (Vulkan-enabled, ~100 MB) ─────────────────────────────
echo "=== Packaging nnserver ==="
mkdir -p dist/nnserver-windows
cp build/nnserver/bin/nnserver.exe dist/nnserver-windows/
cp build/nnserver/bin/onnxruntime.dll dist/nnserver-windows/
cp build/nnserver/bin/model_url.txt dist/nnserver-windows/
for dll in libgomp-1.dll libwinpthread-1.dll libgcc_s_seh-1.dll libstdc++-6.dll vulkan-1.dll; do
    find /mingw64/bin -name "$dll" -exec cp {} dist/nnserver-windows/ \; 2>/dev/null || true
done
cd dist && zip -r ../nnserver-windows.zip nnserver-windows && cd ..
echo "=== nnserver-windows.zip created ==="
