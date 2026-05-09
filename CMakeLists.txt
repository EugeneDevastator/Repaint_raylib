cmake_minimum_required(VERSION 3.14)
project(repaint_raylib C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Point to local sysroot for X11/OpenGL headers/libs (no sudo available)
if(EXISTS "/tmp/local_sysroot/usr")
    message(STATUS "Using local sysroot: /tmp/local_sysroot/usr")
    set(CMAKE_INCLUDE_PATH "/tmp/local_sysroot/usr/include" ${CMAKE_INCLUDE_PATH})
    set(CMAKE_LIBRARY_PATH "/tmp/local_sysroot/usr/lib/x86_64-linux-gnu" "/tmp/local_sysroot/usr/lib" ${CMAKE_LIBRARY_PATH})
endif()

include(FetchContent)
FetchContent_Declare(
    raylib
    URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz
    URL_HASH SHA256=2b3ee1e2120c7a0796b33062c7e9a694dd8a8caa56a96319ac8c8ecf54a90d0b
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(INSTALL_RAYLIB OFF CACHE BOOL "" FORCE)
set(ENABLE_OPENGL_DEBUG ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(raylib)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

set(SOURCES
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
    src/viewport.c
    src/viewport_renderer.c
    src/raygui_impl.c
)

add_executable(repaint ${SOURCES})
target_link_libraries(repaint PRIVATE raylib)

# Explicit X11 + GL linking for static raylib (PRIVATE deps don't propagate)
if(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_link_libraries(repaint PRIVATE ${X11_LIBRARIES})
        message(STATUS "X11_LIBRARIES: ${X11_LIBRARIES}")
    else()
        message(WARNING "X11 not found — link may fail on Linux")
    endif()

    # Transitive static deps for X11 (libxcb → libXau, libXdmcp)
    foreach(lib xcb xcb-xfixes xcb-shape xcb-render Xau Xdmcp)
        find_library(${lib}_LIB NAMES ${lib} PATHS
            /tmp/local_sysroot/usr/lib/x86_64-linux-gnu
            /tmp/local_sysroot/usr/lib
            NO_DEFAULT_PATH
        )
        if(${lib}_LIB)
            target_link_libraries(repaint PRIVATE ${${lib}_LIB})
            message(STATUS "Found lib${lib}: ${${lib}_LIB}")
        endif()
    endforeach()
endif()

add_custom_command(TARGET repaint POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:repaint>/shaders"
    COMMENT "Copying shaders"
)

if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    add_custom_command(TARGET repaint POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:repaint>/resources"
        COMMENT "Copying resources"
    )
endif()

message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "Source files: ${SOURCES}")
