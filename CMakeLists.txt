cmake_minimum_required(VERSION 3.20)
project(repaint_raylib C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti")

option(BUILD_GUI "Build the repaint GUI application" ON)

include(FetchContent)

# ── GUI-specific dependencies ─────────────────────────────────────────────────
if(BUILD_GUI)

# ── detect display server (X11 / Wayland) ────────────────────────────────────
if(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    find_package(PkgConfig QUIET)
    set(_WAYLAND_FOUND FALSE)
    if(PkgConfig_FOUND)
        pkg_check_modules(_WL QUIET wayland-client wayland-cursor xkbcommon)
        if(_WL_FOUND)
            set(_WAYLAND_FOUND TRUE)
        endif()
    endif()

    if(NOT X11_FOUND AND NOT _WAYLAND_FOUND)
        message(FATAL_ERROR
            "\nNeither X11 nor Wayland development libraries were found.\n"
            "Install one of the following before building:\n"
            "  Debian/Ubuntu:  sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxrender-dev libxfixes-dev libxext-dev\n"
            "  Fedora:         sudo dnf install libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel libXrender-devel libXfixes-devel libXext-devel\n"
            "  Arch:           sudo pacman -S libx11 libxrandr libxinerama libxcursor libxi libxrender libxfixes libxext\n"
            "For Wayland:      install libwayland-dev (Debian) / wayland-devel (Fedora) / wayland (Arch)\n")
    endif()

    find_package(OpenGL QUIET)
    if(NOT OPENGL_FOUND)
        message(FATAL_ERROR
            "\nOpenGL development libraries were not found.\n"
            "Install the following:\n"
            "  Debian/Ubuntu:  sudo apt install libgl1-mesa-dev\n"
            "  Fedora:         sudo dnf install mesa-libGL-devel\n"
            "  Arch:           sudo pacman -S mesa\n")
    endif()

    set(GLFW_BUILD_X11     ${X11_FOUND}       CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND ${_WAYLAND_FOUND}   CACHE BOOL "" FORCE)
    message(STATUS "GLFW backends — X11: ${X11_FOUND}, Wayland: ${_WAYLAND_FOUND}")
endif()

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

endif()  # BUILD_GUI

# ── sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/app.cpp
    src/gpu_preference.cpp
    src/modulators.cpp
    src/brush_preset.cpp
    src/compositor.cpp
    src/render_utils.cpp
    src/layerstack.cpp
    src/texture_manager.cpp
    src/repaint_core.cpp
    src/xform.cpp
    src/file_format.cpp
    src/app_config.cpp
    src/usertextures.cpp
    # ── ui ───────────────────────────────────────────────────────────
    src/ui/ui_controls.cpp
    src/ui/info_text.cpp
    src/ui/ui_color.cpp
    src/ui/ui_quickpanel.cpp
    src/ui/ui_brushgizmo.cpp
    src/ui/ui_sliders.cpp
    src/ui/ui_filepanel.cpp
    src/ui/ui_toolbox.cpp
    src/ui/ui_layerpanel.cpp
    src/ui/ui_leftpanel.cpp
    src/ui/ui_changelog.cpp
    src/ui/ui_helpscreen.cpp
    src/ui/ui_tmexplorer.cpp
    src/ui/ui_quickhud.cpp
    src/ui/ui_nnhud.cpp
    src/ui/ui_sdhud.cpp
    src/ui/ui_warphud.cpp
    src/ui/ui_painthud.cpp
    src/ui/ui_layerxform.cpp
    src/ui/ui_canvasxform.cpp
    src/ui/transform_handle.cpp
    src/ui/ui_style.cpp
    src/ui/ui_texpanel.cpp
    src/undo.cpp
    src/replay_recorder.cpp
    src/user_content_receiver.cpp
    # ── http ─────────────────────────────────────────────────────────
    src/http/matte_client.cpp
    src/http/sd_client.cpp
    src/http/upscale_client.cpp
    # ── sensors ───────────────────────────────────────────────────────
    src/sensors/tablet.cpp
    src/sensors/input_filter.cpp
    src/sensors/clipboard.cpp
    # ── drawing ───────────────────────────────────────────────────────
    src/drawing/brush_blend.cpp
    src/drawing/brush_tex.cpp
    src/drawing/viewport.cpp
    src/viewport_manager.cpp
    src/drawing/viewport_hud.cpp
    src/drawing/stroke.cpp
    src/drawing/stroke_engine.cpp
    src/drawing/segment_drawer.cpp
    # ── LocalPlayer ─────────────────────────────────────────────────────
    LocalPlayer/InputQueue.cpp
    LocalPlayer/StrokeEmitter.cpp
    LocalPlayer/StrokeThrottle.cpp
    # ── network ───────────────────────────────────────────────────────
    src/network/serialize.cpp
    src/network/network_broker.cpp
    src/network/sock_platform.cpp
    src/network/test_broker.cpp
    # ── external ──────────────────────────────────────────────────────
    dialog/dialog.c
    raylib_utils/RaylibUtils.c
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)

# Generate icon.ico from icon.png at configure time
set(PNG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png")
set(ICO_DST "${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.ico")
find_program(PYTHON python3)
if(NOT PYTHON)
    find_program(PYTHON python)
endif()
if(EXISTS ${PNG_SRC} AND PYTHON)
    execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/tools/png2ico.py" ${PNG_SRC} ${ICO_DST}
        OUTPUT_QUIET ERROR_QUIET)
endif()

# ── GUI targets ──────────────────────────────────────────────────────────────
if(BUILD_GUI)

add_executable(repaint ${SOURCES})

if(WIN32)
    if(PYTHON AND EXISTS ${PNG_SRC})
        # Regenerate icon.ico whenever icon.png changes (build-time dependency)
        add_custom_command(
            OUTPUT ${ICO_DST}
            COMMAND ${PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/tools/png2ico.py" ${PNG_SRC} ${ICO_DST}
            DEPENDS ${PNG_SRC}
            COMMENT "Generating icon.ico from icon.png..."
        )
        add_custom_target(icon_ico DEPENDS ${ICO_DST})
        add_dependencies(repaint icon_ico)
        target_sources(repaint PRIVATE resources/icon.rc)
    endif()
    target_sources(repaint PRIVATE src/sensors/tablet_win.cpp src/sensors/platform_utils_win.cpp src/sensors/clipboard_win.cpp)
    target_compile_definitions(repaint PRIVATE WINVER=0x0602 _WIN32_WINNT=0x0602)
    target_link_libraries(repaint PRIVATE opengl32 gdi32 winmm user32)
    if(MINGW)
        target_link_options(repaint PRIVATE "-Wl,--allow-multiple-definition")
    endif()
else()
    target_sources(repaint PRIVATE src/sensors/tablet_lnx.cpp src/sensors/platform_utils_lnx.cpp src/sensors/clipboard_lnx.cpp)
endif()

target_include_directories(repaint PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http
    ${CMAKE_CURRENT_SOURCE_DIR}/dialog
    ${CMAKE_CURRENT_SOURCE_DIR}/raylib_utils
    ${CMAKE_CURRENT_SOURCE_DIR}/LocalPlayer
    ${imgui_SOURCE_DIR}
    ${rlimgui_SOURCE_DIR}
)

if(WIN32)
    target_link_libraries(repaint PRIVATE ws2_32)
endif()

target_link_libraries(repaint PRIVATE raylib)

# ── tablet-calibrator (standalone diagnostic tool) ───────────────────────────
set(CALIB_SOURCES tablet-calibrator/main.cpp)
add_executable(tablet-calibrator ${CALIB_SOURCES})

target_include_directories(tablet-calibrator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(WIN32)
    target_sources(tablet-calibrator PRIVATE src/sensors/tablet_win.cpp src/sensors/platform_utils_win.cpp)
    target_compile_definitions(tablet-calibrator PRIVATE WINVER=0x0602 _WIN32_WINNT=0x0602)
    target_link_libraries(tablet-calibrator PRIVATE opengl32 gdi32 winmm user32)
    if(MINGW)
        target_link_options(tablet-calibrator PRIVATE "-Wl,--allow-multiple-definition")
    endif()
else()
    target_sources(tablet-calibrator PRIVATE src/sensors/tablet_lnx.cpp src/sensors/platform_utils_lnx.cpp)
    target_link_libraries(tablet-calibrator PRIVATE m pthread dl)
endif()

target_link_libraries(tablet-calibrator PRIVATE raylib)

endif()  # BUILD_GUI

# ── nnserver is a standalone project built from NNModelServer/ ──────────────
# ── platform linking ──────────────────────────────────────────────────────────
if(BUILD_GUI)
if(UNIX AND NOT APPLE)
    if(X11_FOUND)
        target_link_libraries(repaint PRIVATE ${X11_LIBRARIES})
        target_link_libraries(tablet-calibrator PRIVATE ${X11_LIBRARIES})
    endif()
    if(_WAYLAND_FOUND)
        target_link_libraries(repaint PRIVATE ${_WL_LIBRARIES})
        target_link_libraries(tablet-calibrator PRIVATE ${_WL_LIBRARIES})
    endif()
    target_link_libraries(repaint PRIVATE m pthread dl)
endif()
endif()

# ── resource copies (configure-time + post-build) ───────────────────────────
if(BUILD_GUI)
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
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/CHANGELOG.txt"
            "$<TARGET_FILE_DIR:repaint>/CHANGELOG.txt"
        DEPENDS ${RESOURCE_FILES}
        COMMENT "Force-copying resources to build/bin dirs")
endif()
endif()  # BUILD_GUI