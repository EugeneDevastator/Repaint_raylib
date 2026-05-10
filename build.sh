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
DEP_DIR="$BUILD_DIR/_deps"
RAYLIB_SRC="$DEP_DIR/raylib-src/src"
RAYLIB_LIB="$DEP_DIR/raylib-build/raylib/libraylib.a"
IMGUI_DIR="$DEP_DIR/imgui-src"
RLIMGUI_DIR="$DEP_DIR/rlimgui-src"
OBJ_DIR="$BUILD_DIR/objs"
BIN_DIR="$BUILD_DIR/bin"
BINARY="$BIN_DIR/repaint"

CXXFLAGS="-DGRAPHICS_API_OPENGL_33 -DPLATFORM_DESKTOP"
CXXFLAGS="$CXXFLAGS -I$INC_DIR -I$RAYLIB_SRC -I$IMGUI_DIR -I$RLIMGUI_DIR"
CXXFLAGS="$CXXFLAGS -O3 -DNDEBUG -std=c++11 -fno-exceptions -fno-rtti"

SOURCES=(
    src/main.cpp
    src/repaint_core.cpp
    src/repaint_painter.cpp
    src/repaint_drawmodel.cpp
    src/ui_controls.cpp
    src/ui_color.cpp
    src/ui_gizmo.cpp
    src/ui_layerpanel.cpp
    src/app.cpp
    src/brush_blend.cpp
    src/viewport_renderer.cpp
    src/viewport.cpp
    src/raygui_impl.cpp
    src/stroke.cpp
    $IMGUI_DIR/imgui.cpp
    $IMGUI_DIR/imgui_draw.cpp
    $IMGUI_DIR/imgui_tables.cpp
    $IMGUI_DIR/imgui_widgets.cpp
    $RLIMGUI_DIR/rlImGui.cpp
)

mkdir -p "$OBJ_DIR" "$BIN_DIR"

echo -e "${YELLOW}Compiling...${NC}"
NEED_LINK=0
for src in "${SOURCES[@]}"; do
    srcname=$(basename "$src")
    objname="${srcname}.o"
    obj="$OBJ_DIR/$objname"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ]; then
        echo "  CXX $src"
        /usr/bin/g++ $CXXFLAGS -c "$src" -o "$obj"
        NEED_LINK=1
    fi
done

if [ $NEED_LINK -eq 1 ] || [ ! -f "$BINARY" ]; then
    echo -e "${YELLOW}Linking...${NC}"
    X11_LIBS="-l:libX11.so.6 -l:libGL.so.1 -lm -lpthread"
    OBJS=""
    for src in "${SOURCES[@]}"; do
        srcname=$(basename "$src")
        OBJS="$OBJS $OBJ_DIR/${srcname}.o"
    done
    /usr/bin/g++ -O3 -DNDEBUG \
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
