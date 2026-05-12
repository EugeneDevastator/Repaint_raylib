cmake_minimum_required(VERSION 3.20)
project(repaint_raylib C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")

# ── dependencies ──────────────────────────────────────────────────────────────
include(FetchContent)

# raylib 6.0 — latest stable release as of 2025
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

# imgui v1.92.7
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.7.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)

# rlImGui — Raylib_6_0 branch (raylib 6.0 + imgui 1.92.7)
FetchContent_Declare(
    rlimgui
    URL https://github.com/raylib-extras/rlImGui/archive/refs/tags/Raylib_6_0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(rlimgui)

# ── sources ───────────────────────────────────────────────────────────────────
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
    src/local_broker.cpp
    src/ui_controls.cpp
    src/ui_color.cpp
    src/ui_quickpanel.cpp
    src/ui_brushpreview.cpp
    src/ui_brushgizmo.cpp
    src/ui_sliders.cpp
    src/ui_quickinfo.cpp
    src/ui_filepanel.cpp
    src/ui_toolbox.cpp
    src/ui_layerpanel.cpp
    src/ui_leftpanel.cpp
    src/ui_style.cpp
    src/stroke.cpp
    src/file_format.cpp
    src/network_broker.cpp
    src/app_config.cpp
    src/sock_platform.cpp
    dialog/dialog.c
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)

add_executable(repaint ${SOURCES})

target_include_directories(repaint PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/dialog
    ${imgui_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR}
)

if(WIN32)
    target_link_libraries(repaint PRIVATE ws2_32)
endif()

target_link_libraries(repaint PRIVATE raylib)

# ── platform linking ──────────────────────────────────────────────────────────
if(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    if(X11_FOUND)
        target_link_libraries(repaint PRIVATE ${X11_LIBRARIES})
    endif()
    target_link_libraries(repaint PRIVATE m pthread dl)
endif()

if(WIN32)
    target_link_libraries(repaint PRIVATE opengl32 gdi32 winmm)
endif()

# ── post-build copies ─────────────────────────────────────────────────────────
add_custom_command(TARGET repaint POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:repaint>/shaders")

if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    add_custom_command(TARGET repaint POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:repaint>/resources")
endif()