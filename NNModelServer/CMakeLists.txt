cmake_minimum_required(VERSION 3.20)
project(nnserver C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(NNSERVER_VERSION  "1.21.0" CACHE STRING "ONNX Runtime version")

# Parent of NNModelServer/ — root of shared code
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)

set(NNSERVER_SOURCES
    nnserver.cpp
    nn_onnx.cpp
    matte_module.cpp
    nn_downloader.cpp
    ${PROJECT_ROOT}/src/network/sock_platform.cpp
)

# stb_image headers (single-file, public domain)
set(STB_DIR "${CMAKE_BINARY_DIR}/stb")
if(NOT EXISTS "${STB_DIR}/stb_image.h")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
        "${STB_DIR}/stb_image.h")
endif()
if(NOT EXISTS "${STB_DIR}/stb_image_write.h")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"
        "${STB_DIR}/stb_image_write.h")
endif()

add_executable(nnserver ${NNSERVER_SOURCES})

target_include_directories(nnserver PRIVATE
    ${PROJECT_ROOT}/include
    "${STB_DIR}")

# ── ONNX Runtime + platform-specific deps ──────────────────────────────────
if(WIN32)
    target_sources(nnserver PRIVATE ${PROJECT_ROOT}/src/network/nn_download_win.cpp)

    set(ORT_ROOT "${CMAKE_BINARY_DIR}/onnxruntime" CACHE PATH "ONNX Runtime directory")
    set(ORT_INCLUDE_DIR "${ORT_ROOT}/include")
    set(ORT_LIB_DIR     "${ORT_ROOT}/lib")
    set(ORT_BIN_DIR     "${ORT_LIB_DIR}")

    if(NOT EXISTS "${ORT_INCLUDE_DIR}/onnxruntime_c_api.h")
        message(STATUS "Downloading ONNX Runtime ${NNSERVER_VERSION} for Windows...")
        file(DOWNLOAD
            "https://github.com/microsoft/onnxruntime/releases/download/v${NNSERVER_VERSION}/onnxruntime-win-x64-${NNSERVER_VERSION}.zip"
            "${CMAKE_BINARY_DIR}/onnxruntime.zip" SHOW_PROGRESS)
        file(ARCHIVE_EXTRACT INPUT "${CMAKE_BINARY_DIR}/onnxruntime.zip" DESTINATION "${ORT_ROOT}-tmp")
        file(GLOB _ort_dirs "${ORT_ROOT}-tmp/*")
        list(LENGTH _ort_dirs _ort_n)
        if(_ort_n EQUAL 1)
            file(RENAME "${_ort_dirs}" "${ORT_ROOT}")
        else()
            file(RENAME "${ORT_ROOT}-tmp" "${ORT_ROOT}")
        endif()
        file(REMOVE "${CMAKE_BINARY_DIR}/onnxruntime.zip")
        file(REMOVE_RECURSE "${ORT_ROOT}-tmp")
    endif()

    if(MINGW)
        set(_ort_header "${ORT_INCLUDE_DIR}/onnxruntime_c_api.h")
        file(READ "${_ort_header}" _ort_hdr)
        string(REPLACE "#define ORT_API_CALL _stdcall" "#define ORT_API_CALL" _ort_hdr "${_ort_hdr}")
        string(REPLACE "#define NO_EXCEPTION noexcept" "#define NO_EXCEPTION" _ort_hdr "${_ort_hdr}")
        file(WRITE "${_ort_header}" "${_ort_hdr}")
    endif()

    target_include_directories(nnserver PRIVATE "${ORT_INCLUDE_DIR}")
    target_link_directories(nnserver PRIVATE "${ORT_LIB_DIR}")
    target_link_libraries(nnserver PRIVATE onnxruntime ws2_32 winhttp)

    add_custom_command(TARGET nnserver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ORT_BIN_DIR}/onnxruntime.dll"
            "$<TARGET_FILE_DIR:nnserver>/"
        COMMENT "Copying onnxruntime.dll")

    # Copy MinGW runtime DLLs (needed on machines without MSYS2)
    foreach(_dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        find_file(_${_dll}_path ${_dll}
            PATHS "C:/msys64/mingw64/bin"
                  "$ENV{MINGW_PREFIX}/bin"
                  "$ENV{MINGW_PREFIX}/../bin")
        if(_${_dll}_path)
            add_custom_command(TARGET nnserver POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_${_dll}_path}"
                    "$<TARGET_FILE_DIR:nnserver>/"
                COMMENT "Copying ${_dll}")
        endif()
    endforeach()

    # ── stable-diffusion ─────────────────────────────────────────────
    set(SD_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sd-build")
    set(SD_INCLUDE_DIR "${SD_BUILD_DIR}/include")
    if(WIN32)
        set(SD_PLATFORM "win64")
    elseif(APPLE)
        set(SD_PLATFORM "mac")
    else()
        set(SD_PLATFORM "linux")
    endif()
    set(SD_LIB_DIR     "${SD_BUILD_DIR}/${SD_PLATFORM}")

    if(EXISTS "${SD_LIB_DIR}/libstable-diffusion.a")
        message(STATUS "sd: using prebuilt static library")
        target_sources(nnserver PRIVATE sd_module.cpp)
        target_include_directories(nnserver PRIVATE "${SD_INCLUDE_DIR}")
        target_compile_definitions(nnserver PRIVATE SD_PREBUILT=1)
        target_link_directories(nnserver PRIVATE "${SD_LIB_DIR}")
        target_link_libraries(nnserver PRIVATE
            -Wl,--start-group
            stable-diffusion ggml ggml-base ggml-cpu
            -Wl,--end-group
            -fopenmp)

        # Link Vulkan backend if prebuilt
        if(EXISTS "${SD_LIB_DIR}/ggml-vulkan.a" OR EXISTS "${SD_LIB_DIR}/libggml-vulkan.a")
            find_library(_vulkan_lib vulkan-1
                PATHS "C:/msys64/mingw64/lib"
                      "$ENV{MINGW_PREFIX}/lib")
            if(_vulkan_lib)
                target_link_libraries(nnserver PRIVATE ggml-vulkan "${_vulkan_lib}")
                message(STATUS "vulkan: linking ${_vulkan_lib}")
            else()
                target_link_libraries(nnserver PRIVATE ggml-vulkan -lvulkan-1)
            endif()
        endif()

        find_file(_gomp_dll libgomp-1.dll
            PATHS "C:/msys64/mingw64/bin"
                  "$ENV{MINGW_PREFIX}/bin"
                  "$ENV{MINGW_PREFIX}/../bin")
        if(_gomp_dll)
            add_custom_command(TARGET nnserver POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_gomp_dll}"
                    "$<TARGET_FILE_DIR:nnserver>/"
                COMMENT "Copying libgomp-1.dll")
        endif()

        # Copy Vulkan loader DLL alongside the exe
        find_file(_vulkan_dll vulkan-1.dll
            PATHS "C:/msys64/mingw64/bin"
                  "$ENV{MINGW_PREFIX}/bin"
                  "$ENV{CUDA_PATH}/bin"
                  "$ENV{VULKAN_SDK}/Bin")
        if(_vulkan_dll)
            add_custom_command(TARGET nnserver POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_vulkan_dll}"
                    "$<TARGET_FILE_DIR:nnserver>/"
                COMMENT "Copying vulkan-1.dll")
        endif()
    else()
        message(STATUS "sd: prebuilt lib not found, building from source (FetchContent)")
        include(FetchContent)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")
        FetchContent_Declare(stable-diffusion-cpp
            GIT_REPOSITORY https://github.com/leejet/stable-diffusion.cpp.git
            GIT_TAG        master
            GIT_SHALLOW    TRUE
            GIT_SUBMODULES "ggml")
        find_package(Vulkan QUIET COMPONENTS glslc glslangValidator)
        if(Vulkan_FOUND)
            message(STATUS "vulkan: enabling GPU backend")
            set(SD_VULKAN ON CACHE BOOL "" FORCE)
            set(GGML_VULKAN ON CACHE BOOL "" FORCE)
        else()
            message(STATUS "vulkan: not found — CPU only")
        endif()
        FetchContent_MakeAvailable(stable-diffusion-cpp)

        target_compile_options(stable-diffusion PRIVATE -frtti)
        target_compile_options(ggml PRIVATE -frtti)

        target_sources(nnserver PRIVATE sd_module.cpp)
        target_link_libraries(nnserver PRIVATE stable-diffusion)
    endif()

else()  # ── Linux ──────────────────────────────────────────────────────────
    target_sources(nnserver PRIVATE ${PROJECT_ROOT}/src/network/nn_download_lnx.cpp)

    set(ORT_ROOT "${CMAKE_BINARY_DIR}/onnxruntime" CACHE PATH "ONNX Runtime directory")
    set(ORT_INCLUDE_DIR "${ORT_ROOT}/include")
    set(ORT_LIB_DIR     "${ORT_ROOT}/lib")

    if(NOT EXISTS "${ORT_INCLUDE_DIR}/onnxruntime_c_api.h")
        message(STATUS "Downloading ONNX Runtime ${NNSERVER_VERSION} for Linux...")
        file(DOWNLOAD
            "https://github.com/microsoft/onnxruntime/releases/download/v${NNSERVER_VERSION}/onnxruntime-linux-x64-${NNSERVER_VERSION}.tgz"
            "${CMAKE_BINARY_DIR}/onnxruntime.tgz" SHOW_PROGRESS)
        file(ARCHIVE_EXTRACT INPUT "${CMAKE_BINARY_DIR}/onnxruntime.tgz" DESTINATION "${ORT_ROOT}-tmp")
        file(GLOB _ort_dirs "${ORT_ROOT}-tmp/*")
        list(LENGTH _ort_dirs _ort_n)
        if(_ort_n EQUAL 1)
            file(RENAME "${_ort_dirs}" "${ORT_ROOT}")
        else()
            file(RENAME "${ORT_ROOT}-tmp" "${ORT_ROOT}")
        endif()
        file(REMOVE "${CMAKE_BINARY_DIR}/onnxruntime.tgz")
        file(REMOVE_RECURSE "${ORT_ROOT}-tmp")
    endif()

    target_include_directories(nnserver PRIVATE "${ORT_INCLUDE_DIR}")
    target_link_directories(nnserver PRIVATE "${ORT_LIB_DIR}")
    target_link_libraries(nnserver PRIVATE onnxruntime pthread dl curl)

    # ── stable-diffusion (built from source via FetchContent) ────────────
    include(FetchContent)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")

    FetchContent_Declare(stable-diffusion-cpp
        GIT_REPOSITORY https://github.com/leejet/stable-diffusion.cpp.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
        GIT_SUBMODULES "ggml")
    find_package(Vulkan QUIET COMPONENTS glslc glslangValidator)
    if(Vulkan_FOUND)
        message(STATUS "vulkan: enabling GPU backend")
        set(SD_VULKAN ON CACHE BOOL "" FORCE)
        set(GGML_VULKAN ON CACHE BOOL "" FORCE)
    else()
        message(STATUS "vulkan: not found — CPU only")
    endif()
    FetchContent_MakeAvailable(stable-diffusion-cpp)

    target_compile_options(stable-diffusion PRIVATE -frtti)
    target_compile_options(ggml PRIVATE -frtti)

    target_sources(nnserver PRIVATE sd_module.cpp)
    target_link_libraries(nnserver PRIVATE stable-diffusion)

    # Copy ONNX Runtime shared library alongside the exe
    file(GLOB _ort_so "${ORT_LIB_DIR}/libonnxruntime.so*")
    foreach(_f ${_ort_so})
        add_custom_command(TARGET nnserver POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_f}"
                "$<TARGET_FILE_DIR:nnserver>/"
            COMMENT "Copying onnxruntime.so")
    endforeach()
endif()

# model URL file (common to both platforms)
add_custom_command(TARGET nnserver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/model_url.txt"
        "$<TARGET_FILE_DIR:nnserver>/model_url.txt"
    COMMENT "Copying model_url.txt")
