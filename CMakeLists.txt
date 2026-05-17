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
    src/modulators.cpp
    src/layermanager.cpp
    src/repaint_core.cpp
    src/repaint_painter.cpp
    src/serialize.cpp
    src/brush_blend.cpp
    src/viewport.cpp
    src/viewport_renderer.cpp
    src/raygui_impl.cpp
    src/local_broker.cpp
    src/ui_controls.cpp
    src/ui_color.cpp
    src/ui_quickpanel.cpp
    src/viewport_hud.cpp
    src/ui_brushgizmo.cpp
    src/ui_sliders.cpp
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
    src/input_filter.cpp
    src/brush_interpolator.cpp
    src/brush_tex.cpp
    src/usertextures.cpp
    src/test_broker.cpp
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

# ── resource copies (configure-time + post-build) ───────────────────────────
# Force-copy always by removing destination dir first, then re-copying
set(RESOURCE_SHADER_FILES
    "${CMAKE_SOURCE_DIR}/shaders/*")

add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "${CMAKE_BINARY_DIR}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "$<TARGET_FILE_DIR:repaint>/shaders"
    DEPENDS ${RESOURCE_SHADER_FILES}
    COMMENT "Force-copying shaders to build/bin dirs")

if(EXISTS "${CMAKE_SOURCE_DIR}/resources")
    set(RESOURCE_FILES
        "${CMAKE_SOURCE_DIR}/resources/btnnew.png"
        "${CMAKE_SOURCE_DIR}/resources/btnopen.png"
        "${CMAKE_SOURCE_DIR}/resources/btnsaveas.png"
        "${CMAKE_SOURCE_DIR}/resources/btnsave.png"
        "${CMAKE_SOURCE_DIR}/resources/btnreload.png"
        "${CMAKE_SOURCE_DIR}/resources/btnsnap.png"
        "${CMAKE_SOURCE_DIR}/resources/btnpin.png")
    add_custom_target(copy_resources ALL
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/resources"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources"
            "${CMAKE_BINARY_DIR}/resources"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:repaint>/resources"
        DEPENDS ${RESOURCE_FILES}
        COMMENT "Force-copying resources to build/bin dirs")
endif()