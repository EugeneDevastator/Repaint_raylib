cmake_minimum_required(VERSION 3.14)
project(repaint_raylib CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ── strict subset: no C++ bloat ───────────────────────────────────────────────
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")

if(EXISTS "/tmp/local_sysroot/usr")
    set(CMAKE_INCLUDE_PATH "/tmp/local_sysroot/usr/include" ${CMAKE_INCLUDE_PATH})
    set(CMAKE_LIBRARY_PATH
        "/tmp/local_sysroot/usr/lib/x86_64-linux-gnu"
        "/tmp/local_sysroot/usr/lib"
        ${CMAKE_LIBRARY_PATH})
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
FetchContent_MakeAvailable(raylib)

FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
    rlimgui
    URL https://github.com/raylib-extras/rlImGui/archive/refs/heads/main.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(rlimgui)

# ── sources (all .cpp, C-style code) ─────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/repaint_core.cpp
    src/repaint_painter.cpp
    src/repaint_drawmodel.cpp
    src/brush_blend.cpp
    src/viewport.cpp
    src/viewport_renderer.cpp
    src/raygui_impl.cpp
    src/ui_controls.cpp
    src/ui_color.cpp
    src/ui_gizmo.cpp
    src/ui_layerpanel.cpp
)

# ── imgui (C++ but we don't touch these) ─────────────────────────────────────
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

set(RLIMGUI_SOURCES
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)

add_executable(repaint ${SOURCES} ${IMGUI_SOURCES} ${RLIMGUI_SOURCES})

target_include_directories(repaint PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${imgui_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR}
)

target_link_libraries(repaint PRIVATE raylib)

if(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_link_libraries(repaint PRIVATE ${X11_LIBRARIES})
    endif()

    foreach(lib xcb xcb-xfixes xcb-shape xcb-render Xau Xdmcp)
        find_library(${lib}_LIB NAMES ${lib} PATHS
            /tmp/local_sysroot/usr/lib/x86_64-linux-gnu
            /tmp/local_sysroot/usr/lib
            NO_DEFAULT_PATH)
        if(${lib}_LIB)
            target_link_libraries(repaint PRIVATE ${${lib}_LIB})
        endif()
    endforeach()
endif()

add_custom_command(TARGET repaint POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:repaint>/shaders"
    COMMENT "Copying shaders")

if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    add_custom_command(TARGET repaint POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:repaint>/resources"
        COMMENT "Copying resources")
endif()
