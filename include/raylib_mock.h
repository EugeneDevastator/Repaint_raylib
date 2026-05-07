// Minimal raylib mock header for structure demonstration
// Replace with actual raylib when building

#ifndef RAYLIB_MOCK_H
#define RAYLIB_MOCK_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>

// Basic types
typedef struct Vector2 { float x; float y; } Vector2;
typedef struct Vector3 { float x; float y; float z; } Vector3;
typedef struct Rectangle { float x; float y; float width; float height; } Rectangle;
typedef struct Color { unsigned char r; unsigned char g; unsigned char b; unsigned char a; } Color;

// Texture2D needs width/height for the code to work
typedef struct Texture2D {
    unsigned int id;        // OpenGL texture id
    int width;              // Texture width
    int height;             // Texture height
    int mipmaps;           // Mipmap levels
    int format;             // Data format (PixelFormat)
} Texture2D;

typedef struct Image {
    void* data;            // Image data
    int width;              // Image width
    int height;             // Image height
    int mipmaps;           // Mipmap levels
    int format;             // Data format (PixelFormat)
} Image;

typedef struct RenderTexture2D {
    unsigned int id;        // OpenGL framebuffer object id
    Texture2D texture;      // Color buffer texture
    Texture2D depth;        // Depth buffer texture
} RenderTexture2D;

typedef struct Shader {
    unsigned int id;        // Shader program id
    int* locs;             // Shader locations array
} Shader;

// Color constants
static const Color BLANK = {0, 0, 0, 0};
static const Color WHITE = {255, 255, 255, 255};
static const Color BLACK = {0, 0, 0, 255};
static const Color GRAY = {128, 128, 128, 255};
static const Color LIGHTGRAY = {192, 192, 192, 255};
static const Color DARKGRAY = {64, 64, 64, 255};
static const Color GREEN = {0, 255, 0, 255};
static const Color YELLOW = {255, 255, 0, 255};
static const Color RED = {255, 0, 0, 255};

// Mouse buttons
#define MOUSE_LEFT_BUTTON 0
#define MOUSE_RIGHT_BUTTON 1

// Window/Input functions (stubs)
static inline void InitWindow(int width, int height, const char* title) {}
static inline bool WindowShouldClose() { return false; }
static inline void CloseWindow() {}
static inline void SetTargetFPS(int fps) {}
static inline void BeginDrawing() {}
static inline void EndDrawing() {}
static inline bool IsMouseButtonDown(int button) { return false; }
static inline Vector2 GetMousePosition() { Vector2 v = {0, 0}; return v; }
static inline float GetMouseWheelMove() { return 0.0f; }
static inline int GetKeyPressed() { return 0; }

// Drawing functions (stubs)
static inline void ClearBackground(Color color) {}
static inline void DrawRectangle(int x, int y, int width, int height, Color color) {}
static inline void DrawRectangleRec(Rectangle rec, Color color) {}
static inline void DrawRectangleLinesEx(Rectangle rec, int lineThick, Color color) {}
static inline void DrawText(const char* text, int x, int y, int fontSize, Color color) {}
static inline int MeasureText(const char* text, int fontSize) { return 0; }
static inline bool CheckCollisionPointRec(Vector2 point, Rectangle rec) { return false; }

// Texture/Image functions (stubs)
static inline Image GenImageColor(int width, int height, Color color) { Image img = {0}; return img; }
static inline Texture2D LoadTextureFromImage(Image image) { Texture2D tex = {0}; return tex; }
static inline Image LoadImageFromTexture(Texture2D texture) { Image img = {0}; return img; }
static inline void UnloadImage(Image image) {}
static inline void UnloadTexture(Texture2D texture) {}
static inline void ImageDrawPixel(Image* image, int x, int y, Color color) {}
static inline void ImageDrawCircleV(Image* image, Vector2 center, int radius, Color color) {}

// Shader functions (stubs)
static inline Shader LoadShader(const char* vsFileName, const char* fsFileName) { Shader s = {0}; return s; }
static inline void UnloadShader(Shader shader) {}

// RenderTexture functions (stubs)
static inline RenderTexture2D LoadRenderTexture(int width, int height) { RenderTexture2D rt = {0}; return rt; }
static inline void BeginTextureMode(RenderTexture2D target) {}
static inline void EndTextureMode() {}
static inline void DrawTexture(Texture2D texture, int x, int y, Color tint) {}
static inline void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle dest, Vector2 origin, float rotation, Color tint) {}

// Math constants
#ifndef PI
#define PI 3.14159265358979323846
#endif

#endif // RAYLIB_MOCK_H
