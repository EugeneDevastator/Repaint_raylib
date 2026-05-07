cmake_minimum_required(VERSION 3.10)
project(repaint_raylib C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Try to find raylib
find_package(raylib QUIET)

if (NOT raylib_FOUND)
    # Try to find raylib in system paths
    find_library(RAYLIB_LIBRARY
        NAMES raylib libraylib.a
        PATHS
        /usr/lib
        /usr/local/lib
        /mingw64/lib
        /mingw32/lib
        ${RAYLIB_PATH}/lib
        /opt/homebrew/lib
    )

    find_path(RAYLIB_INCLUDE_DIR
        NAMES raylib.h
        PATHS
        /usr/include
        /usr/local/include
        /mingw64/include
        /mingw32/include
        ${RAYLIB_PATH}/include
        /opt/homebrew/include
    )

    if (RAYLIB_LIBRARY AND RAYLIB_INCLUDE_DIR)
        add_library(raylib SHARED IMPORTED)
        set_target_properties(raylib PROPERTIES
            IMPORTED_LOCATION "${RAYLIB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${RAYLIB_INCLUDE_DIR}"
        )
        message(STATUS "Found raylib: ${RAYLIB_LIBRARY}")
    else()
        message(WARNING "raylib not found. Attempting to use pkg-config...")
        # Try pkg-config as last resort
        find_package(PkgConfig QUIET)
        if (PKG_CONFIG_FOUND)
            pkg_check_modules(RAYLIB raylib)
            if (RAYLIB_FOUND)
                add_library(raylib SHARED IMPORTED)
                set_target_properties(raylib PROPERTIES
                    IMPORTED_LOCATION "${RAYLIB_LIBRARY_DIRS}/libraylib.so"
                    INTERFACE_INCLUDE_DIRECTORIES "${RAYLIB_INCLUDE_DIRS}"
                )
            endif()
        endif()
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
if (RAYLIB_INCLUDE_DIR)
    include_directories(${RAYLIB_INCLUDE_DIR})
endif()

# Source files
set(SOURCES
    src/main.c
    src/repaint_core.c
)

# Shaders (will be copied to build directory)
set(SHADERS
    shaders/brush_gen.vs
    shaders/brush_gen.fs
    shaders/brush_render.fs
)

# Create executable
add_executable(repaint ${SOURCES})

# Link libraries
if (TARGET raylib)
    target_link_libraries(repaint raylib)
else()
    target_link_libraries(repaint ${RAYLIB_LIBRARIES})
endif()

# Copy shaders to build directory
foreach(shader ${SHADERS})
    add_custom_command(
        TARGET repaint POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/${shader}"
        "$<TARGET_FILE_DIR:repaint>/${shader}"
        COMMENT "Copying ${shader} to build directory"
    )
endforeach()

# Copy resources folder if it exists
if (EXISTS "${CMAKE_SOURCE_DIR}/resources")
    add_custom_command(
        TARGET repaint POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/resources"
        "$<TARGET_FILE_DIR:repaint>/resources"
        COMMENT "Copying resources to build directory"
    )
endif()

# Platform-specific settings
if (WIN32)
    # Windows specific settings
    target_link_libraries(repaint opengl32 gdi32 winmm)
    set_target_properties(repaint PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
elseif (UNIX)
    # Unix/Linux specific settings
    target_link_libraries(repaint m pthread dl)
    if (APPLE)
        target_link_libraries(repaint "-framework CoreVideo -framework IOKit -framework Cocoa -framework GLUT -framework OpenGL")
    else()
        target_link_libraries(repaint GL)
    endif()
endif()

# Install target (optional)
install(TARGETS repaint DESTINATION bin)
install(FILES ${SHADERS} DESTINATION bin/shaders)
if (EXISTS "${CMAKE_SOURCE_DIR}/resources")
    install(DIRECTORY resources DESTINATION bin)
endif()

message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "Source files: ${SOURCES}")
message(STATUS "If raylib not found, please install it or set RAYLIB_PATH")
