#!/bin/bash
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== RePaint Raylib Build ===${NC}"

SRC_DIR="src"
INC_DIR="include"
BUILD_DIR="build"
RAYLIB_SRC="$BUILD_DIR/_deps/raylib-src/src"
RAYLIB_LIB="$BUILD_DIR/_deps/raylib-build/raylib/libraylib.a"
OBJ_DIR="$BUILD_DIR/CMakeFiles/repaint.dir/src"
BIN_DIR="$BUILD_DIR/bin"
BINARY="$BIN_DIR/repaint"

CFLAGS="-DGRAPHICS_API_OPENGL_33 -DPLATFORM_DESKTOP -I$INC_DIR -I$RAYLIB_SRC -O3 -DNDEBUG -std=gnu99"

SOURCES=(
    src/main.c
    src/repaint_core.c
    src/repaint_painter.c
    src/repaint_drawmodel.c
    src/ui_controls.c
    src/ui_color.c
    src/ui_gizmo.c
    src/ui_layerpanel.c
    src/app.c
    src/brush_blend.c
    src/viewport_renderer.c
    src/viewport.c
)

mkdir -p "$OBJ_DIR" "$BIN_DIR"

echo -e "${YELLOW}Compiling...${NC}"
NEED_LINK=0
for src in "${SOURCES[@]}"; do
    basename=$(basename "$src" .c)
    obj="$OBJ_DIR/${basename}.c.o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "  CC $src"
        /usr/bin/cc $CFLAGS -c "$src" -o "$obj"
        NEED_LINK=1
    fi
done

if [ $NEED_LINK -eq 1 ] || [ ! -f "$BINARY" ]; then
    echo -e "${YELLOW}Linking...${NC}"
    # Use versioned .so filenames (no dev symlinks available on this system)
    X11_LIBS="-l:libX11.so.6 -l:libGL.so.1 -lm"
    OBJS=""
    for src in "${SOURCES[@]}"; do
        basename=$(basename "$src" .c)
        OBJS="$OBJS $OBJ_DIR/${basename}.c.o"
    done
    /usr/bin/cc -O3 -DNDEBUG \
        $OBJS \
        -o "$BINARY" \
        "$RAYLIB_LIB" $X11_LIBS
    echo -e "${GREEN}Linked: $BINARY${NC}"
else
    echo -e "${GREEN}Everything up to date.${NC}"
fi

echo -e "${YELLOW}Copying shaders...${NC}"
mkdir -p "$BIN_DIR/shaders"
cp -r shaders/* "$BIN_DIR/shaders/" 2>/dev/null || true

if [ -d "resources" ]; then
    echo -e "${YELLOW}Copying resources...${NC}"
    mkdir -p "$BIN_DIR/resources"
    cp -r resources/* "$BIN_DIR/resources/" 2>/dev/null || true
fi

echo -e "${GREEN}=== Build completed! ===${NC}"
echo -e "${GREEN}Run: $BINARY${NC}"
