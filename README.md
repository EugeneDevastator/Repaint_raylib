# RePaint Raylib Port

A port of the repaint-qt project from C++/Qt to pure C with raylib.

## Changes Made

1. **Replaced Qt with raylib** - Pure C implementation using raylib for graphics
2. **Replaced scanline methods with shaders** - Custom fragment shaders handle brush generation and rendering
3. **Simplified UI with rectangles** - UI elements are now simple rectangles instead of Qt widgets

## Project Structure

```
repaint_raylib/
├── include/
│   └── repaint.h          # Main header with data structures
├── src/
│   ├── main.c              # Main application with UI
│   └── repaint_core.c     # Core logic ported from C++
├── shaders/
│   ├── brush_gen.vs       # Vertex shader for brush generation
│   ├── brush_gen.fs       # Fragment shader for brush generation
│   └── brush_render.fs   # Fragment shader for brush rendering
├── resources/              # Resources folder (optional)
├── CMakeLists.txt         # CMake build configuration
├── build.sh              # Build script for MSYS2/MinGW
└── README.md
```

## Building on Windows with MSYS2/MinGW

1. Install MSYS2 if not already installed
2. Open MSYS2 MinGW 64-bit terminal
3. Install required packages:
   ```bash
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake
   ```

4. Install raylib:
   ```bash
   cd ~
   git clone https://github.com/raysan5/raylib.git
   cd raylib
   mkdir build && cd build
   cmake -DBUILD_SHARED_LIBS=OFF ..
   make
   make install
   ```

5. Build the project:
   ```bash
   cd /path/to/repaint_raylib
   ./build.sh Release
   ```

6. Run:
   ```bash
   cd build/bin
   ./repaint.exe
   ```

## Building on Linux

```bash
mkdir build && cd build
cmake ..
make
./bin/repaint
```

## Features Ported

- Brush rendering with customizable size, hardness, opacity
- Multiple brush tools (Brush, Smudge, Line, Eraser)
- Layer management (add, delete, opacity, visibility)
- Canvas zoom and pan
- Simple rectangle-based UI

## Differences from Original

1. **UI** - Replaced Qt widgets with simple rectangular UI elements drawn with raylib
2. **Rendering** - Replaced QPainter/scanline with OpenGL shaders
3. **Language** - Ported from C++ to pure C
4. **Dependencies** - Only requires raylib (no Qt needed)

## Shader-Based Rendering

The original scanline-based brush generation has been replaced with GPU shaders:

- `brush_gen.fs` - Generates brush textures with radial gradients, curvature, and noise
- `brush_render.fs` - Handles blending modes when applying brush to canvas

## License

Same as original repaint-qt (GPL v3)
