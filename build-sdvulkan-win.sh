#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════
#  build-sdvulkan-win.sh — Build SD prebuilt libs with Vulkan (MSYS2)
#
#  Run once from MSYS2 MinGW64 shell at project root:
#    cd repaint_raylib
#    bash build-sdvulkan-win.sh
#
#  Requires: Vulkan SDK (installed automatically via pacman)
#            curl + tar (present in any MSYS2 install)
#  Output:   NNModelServer/sd-build/win64/*.a
#            NNModelServer/sd-build/include/stable-diffusion.h
# ═══════════════════════════════════════════════════════════════════════

set -euo pipefail

# ─── Pinned versions (bump these when updating sd.cpp) ────────────────
SD_COMMIT="7b5f34d"                          # stable-diffusion.cpp commit hash
GGML_COMMIT="eced84c8"                       # ggml submodule commit (pinned to match sd.cpp)

PROJECT_ROOT="$(cd "$(dirname "$0")" && pwd)"
OUTDIR="$PROJECT_ROOT/NNModelServer/sd-build"

# ─── 1. Install Vulkan dev packages ───────────────────────────────────
echo "=== Installing Vulkan packages..."
pacman -S --noconfirm --needed \
    mingw-w64-x86_64-vulkan-headers \
    mingw-w64-x86_64-vulkan-loader

# ─── 2. Temp directory (auto-cleaned on exit) ────────────────────────
TMPDIR="$(mktemp -d)"
trap "rm -rf \"$TMPDIR\"" EXIT
cd "$TMPDIR"

# ─── 3. Download sd.cpp source tarball (no git needed) ───────────────
echo "=== Downloading stable-diffusion.cpp @ $SD_COMMIT..."
curl -sL "https://github.com/leejet/stable-diffusion.cpp/archive/${SD_COMMIT}.tar.gz" -o sd.tar.gz
tar -xzf sd.tar.gz
rm sd.tar.gz
# GitHub tarball extracts to stable-diffusion.cpp-<commit>
cd stable-diffusion.cpp-*

# ─── 4. Download ggml submodule into place ────────────────────────────
echo "=== Downloading ggml submodule..."
mkdir -p ggml
cd ggml
curl -sL "https://github.com/leejet/ggml/archive/${GGML_COMMIT}.tar.gz" -o ggml.tar.gz
tar -xzf ggml.tar.gz --strip-components=1
rm ggml.tar.gz
cd ..

# ─── 5. Build with Vulkan ────────────────────────────────────────────
echo "=== Building with Vulkan backend..."
cmake -B build \
    -DSD_VULKAN=ON \
    -DSD_BUILD_EXAMPLES=OFF \
    -DSD_BUILD_SHARED_LIBS=OFF \
    .
cmake --build build --target stable-diffusion --parallel "$(nproc)"

# ─── 6. Copy outputs to the per-platform directory ───────────────────
mkdir -p "$OUTDIR/win64" "$OUTDIR/include"

echo "=== Copying .a files to $OUTDIR/win64/..."
cp build/libstable-diffusion.a  "$OUTDIR/win64/"
find build/ggml -name '*.a' -exec cp {} "$OUTDIR/win64/" \;

# Rename ggml-*.a → libggml-*.a (linker expects lib prefix for -l flags)
# Remove old stale copies without lib prefix to avoid duplicates
for f in "$OUTDIR/win64/"ggml*.a "$OUTDIR/win64/"ggml-*.a; do
    [ ! -f "$f" ] && continue
    bname="$(basename "$f")"
    case "$bname" in
        lib*) ;;  # already has lib prefix, keep
        *)     mv "$f" "$OUTDIR/win64/lib${bname}" 2>/dev/null || true ;;
    esac
done

echo "=== Copying header to $OUTDIR/include/..."
cp include/stable-diffusion.h   "$OUTDIR/include/"

echo ""
echo "=== Done! Prebuilt libs with Vulkan are in:"
echo "    $OUTDIR/win64/"
echo "    $OUTDIR/include/"
echo ""
echo "    Commit them:"
echo "      git add NNModelServer/sd-build/ && git commit"
echo ""
ls -lh "$OUTDIR/win64/"
