#pragma once
/*
 * RaylibUtils.h - C-compatible raylib utilities.
 * Compiles under both C and C++ with no changes.
 *
 * Provides: LoadShaderWithIncludes — shader #include preprocessor.
 * #include directives must start at column 0 of a line.
 */

#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

// Load a shader, preprocessing #include directives recursively.
// #include "filename.ext" or #include filename.ext — relative to the
// directory of the file containing the directive (max depth 16).
// vsFileName or fsFileName can be NULL (uses raylib default).
Shader LoadShaderWithIncludes(const char *vsFileName, const char *fsFileName);

#ifdef __cplusplus
}
#endif
