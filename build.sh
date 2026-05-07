#!/bin/bash
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== RePaint Raylib Build ===${NC}"

BUILD_TYPE=${1:-Release}
BUILD_DIR=${2:-build}

if ! command -v cmake &> /dev/null; then
    echo -e "${RED}cmake not found. Please install cmake.${NC}"
    exit 1
fi

SYSROOT="/tmp/local_sysroot/usr"
if [ -d "${SYSROOT}" ]; then
    echo -e "${YELLOW}Local sysroot found: ${SYSROOT}${NC}"
    export PKG_CONFIG_PATH="${SYSROOT}/lib/x86_64-linux-gnu/pkgconfig${PKG_CONFIG_PATH:+:${PKG_CONFIG_PATH}}"
fi

echo -e "${YELLOW}Build type: ${BUILD_TYPE}${NC}"
echo -e "${YELLOW}Build directory: ${BUILD_DIR}${NC}"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo -e "${YELLOW}Configuring...${NC}"
cmake .. -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
    -DCMAKE_INCLUDE_PATH="${SYSROOT}/include" \
    -DCMAKE_LIBRARY_PATH="${SYSROOT}/lib/x86_64-linux-gnu;${SYSROOT}/lib"

echo -e "${YELLOW}Building...${NC}"
cmake --build . --parallel

echo -e "${GREEN}=== Build completed! ===${NC}"
echo -e "${GREEN}Run: ${BUILD_DIR}/bin/repaint${NC}"
