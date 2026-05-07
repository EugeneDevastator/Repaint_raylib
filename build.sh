#!/bin/bash

# Build script for MSYS2/MinGW on Windows
# This script builds the repaint_raylib project using CMake

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== RePaint Raylib Build Script (MSYS2/MinGW) ===${NC}"

# Check if we're in MSYS2/Mingw environment
if [[ -z "$MSYSTEM" ]]; then
    echo -e "${YELLOW}Warning: Not running in MSYS2 environment${NC}"
fi

# Default build type
BUILD_TYPE=${1:-Release}
BUILD_DIR=${2:-build}

echo -e "${GREEN}Build type: ${BUILD_TYPE}${NC}"
echo -e "${GREEN}Build directory: ${BUILD_DIR}${NC}"

# Check for required tools
echo -e "${YELLOW}Checking required tools...${NC}"

if ! command -v cmake &> /dev/null; then
    echo -e "${RED}Error: cmake not found. Please install cmake.${NC}"
    echo "Try: pacman -S mingw-w64-x86_64-cmake"
    exit 1
fi

if ! command -v gcc &> /dev/null; then
    echo -e "${RED}Error: gcc not found. Please install gcc.${NC}"
    echo "Try: pacman -S mingw-w64-x86_64-gcc"
    exit 1
fi

echo -e "${GREEN}All required tools found.${NC}"

# Check for raylib
echo -e "${YELLOW}Checking for raylib...${NC}"

RAYLIB_PATH=""
if [[ -f "/mingw64/lib/libraylib.a" ]]; then
    RAYLIB_PATH="/mingw64"
    echo -e "${GREEN}Found raylib in /mingw64${NC}"
elif [[ -f "/mingw32/lib/libraylib.a" ]]; then
    RAYLIB_PATH="/mingw32"
    echo -e "${GREEN}Found raylib in /mingw32${NC}"
elif [[ -f "/usr/local/lib/libraylib.a" ]]; then
    RAYLIB_PATH="/usr/local"
    echo -e "${GREEN}Found raylib in /usr/local${NC}"
else
    echo -e "${YELLOW}raylib not found. Attempting to install...${NC}"
    echo "You can manually install raylib with:"
    echo "  cd ~"
    echo "  git clone https://github.com/raysan5/raylib.git"
    echo "  cd raylib"
    echo "  mkdir build && cd build"
    echo "  cmake -DBUILD_SHARED_LIBS=OFF .."
    echo "  make"
    echo "  make install"
    exit 1
fi

# Export RAYLIB_PATH for CMake
export RAYLIB_PATH

# Create build directory
echo -e "${YELLOW}Creating build directory...${NC}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Run CMake
echo -e "${YELLOW}Running CMake...${NC}"
if ! cmake .. \
    -G "MSYS Makefiles" \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DRAYLIB_PATH="${RAYLIB_PATH}"; then
    echo -e "${RED}CMake configuration failed${NC}"
    exit 1
fi

# Build
echo -e "${YELLOW}Building project...${NC}"
if ! make -j$(nproc); then
    echo -e "${RED}Build failed${NC}"
    exit 1
fi

echo -e "${GREEN}=== Build completed successfully! ===${NC}"
echo -e "${GREEN}Executable location: ${BUILD_DIR}/bin/repaint.exe${NC}"
echo ""
echo "To run the application:"
echo "  cd ${BUILD_DIR}"
echo "  ./bin/repaint.exe"
echo ""
echo -e "${YELLOW}Note: Make sure the shaders folder is in the same directory as the executable${NC}"

cd ..
