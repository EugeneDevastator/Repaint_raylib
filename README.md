# RePaint Raylib Port
Now with shaders,
AND PROPER BLENDING. Not your normal normal

# Releases:

| Platform | Download |
|----------|----------|
| Windows-x64  | [repaint-windows.zip](https://github.com/EugeneDevastator/Repaint_raylib/releases/download/latest/repaint-windows.zip) |
| Linux    | [repaint-linux.zip](https://github.com/EugeneDevastator/Repaint_raylib/releases/download/latest/repaint-linux.zip) |


A pure C port of the repaint-qt project (C++/Qt painting application) using raylib.

## Build

```bash
./build.sh
```

This automatically fetches raylib 6.0 from GitHub and builds everything.

Requirements: cmake, git, C compiler (gcc/clang), and system dependencies for raylib.

On Ubuntu/Debian:
```bash
sudo apt install cmake gcc git libgl1-mesa-dev libx11-dev libxcursor-dev libxrandr-dev libxi-dev libxinerama-dev
```

## Run

```bash
./build/bin/repaint
```
