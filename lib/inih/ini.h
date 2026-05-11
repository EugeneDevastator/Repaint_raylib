/* inih -- simple .INI file parser

SPDX-License-Identifier: BSD-3-Clause

Copyright (C) 2009-2025, Ben Hoyt

inih is released under the New BSD license (see LICENSE.txt). Go to the project
home page for more info:

https://github.com/benhoyt/inih

*/

#ifndef INI_H
#define INI_H

/* Make this header file easier to include in C++ code */
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

#ifndef INI_HANDLER_LINENO
#define INI_HANDLER_LINENO 0
#endif

#ifndef INI_API
#if defined _WIN32 || defined __CYGWIN__
#	ifdef INI_SHARED_LIB
#		ifdef INI_SHARED_LIB_BUILDING
#			define INI_API __declspec(dllexport)
#		else
#			define INI_API __declspec(dllimport)
#		endif
#	else
#		define INI_API
#	endif
#else
#	if defined(__GNUC__) && __GNUC__ >= 4
#		define INI_API __attribute__ ((visibility ("default")))
#	else
#		define INI_API
#	endif
#endif
#endif

#if INI_HANDLER_LINENO
typedef int (*ini_handler)(void* user, const char* section,
                           const char* name, const char* value,
                           int lineno);
#else
typedef int (*ini_handler)(void* user, const char* section,
                           const char* name, const char* value);
#endif

typedef char* (*ini_reader)(char* str, int num, void* stream);

INI_API int ini_parse(const char* filename, ini_handler handler, void* user);
INI_API int ini_parse_file(FILE* file, ini_handler handler, void* user);
INI_API int ini_parse_stream(ini_reader reader, void* stream, ini_handler handler,
                     void* user);
INI_API int ini_parse_string(const char* string, ini_handler handler, void* user);
INI_API int ini_parse_string_length(const char* string, size_t length,
                            ini_handler handler, void* user);

#ifndef INI_ALLOW_MULTILINE
#define INI_ALLOW_MULTILINE 1
#endif

#ifndef INI_ALLOW_BOM
#define INI_ALLOW_BOM 1
#endif

#ifndef INI_START_COMMENT_PREFIXES
#define INI_START_COMMENT_PREFIXES ";#"
#endif

#ifndef INI_ALLOW_INLINE_COMMENTS
#define INI_ALLOW_INLINE_COMMENTS 1
#endif
#ifndef INI_INLINE_COMMENT_PREFIXES
#define INI_INLINE_COMMENT_PREFIXES ";"
#endif

#ifndef INI_USE_STACK
#define INI_USE_STACK 1
#endif

#ifndef INI_MAX_LINE
#define INI_MAX_LINE 200
#endif

#ifndef INI_ALLOW_REALLOC
#define INI_ALLOW_REALLOC 0
#endif

#ifndef INI_INITIAL_ALLOC
#define INI_INITIAL_ALLOC 200
#endif

#ifndef INI_STOP_ON_FIRST_ERROR
#define INI_STOP_ON_FIRST_ERROR 0
#endif

#ifndef INI_CALL_HANDLER_ON_NEW_SECTION
#define INI_CALL_HANDLER_ON_NEW_SECTION 0
#endif

#ifndef INI_ALLOW_NO_VALUE
#define INI_ALLOW_NO_VALUE 0
#endif

#ifndef INI_CUSTOM_ALLOCATOR
#define INI_CUSTOM_ALLOCATOR 0
#endif

#ifdef __cplusplus
}
#endif

#endif /* INI_H */
