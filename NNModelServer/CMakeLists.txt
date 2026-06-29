# ── nnserver (standalone matting inference server) ─────────────────────────────
set(NNSERVER_VERSION  "1.21.0" CACHE STRING "ONNX Runtime version")
set(NNSERVER_SOURCES
    nnserver.cpp
    nn_onnx.cpp
    matte_module.cpp
    ${CMAKE_SOURCE_DIR}/src/network/sock_platform.cpp
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

# Create target early so platform-specific sections can reference it
add_executable(nnserver ${NNSERVER_SOURCES})

target_include_directories(nnserver PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    "${STB_DIR}")

# ── ONNX Runtime + platform-specific deps ──────────────────────────────────
if(WIN32)
    target_sources(nnserver PRIVATE ${CMAKE_SOURCE_DIR}/src/network/nn_download_win.cpp)

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

    # ── stable-diffusion (prebuilt static libs, Windows only) ──────────────
    set(SD_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sd-build")
    set(SD_INCLUDE_DIR "${SD_BUILD_DIR}/include")
    set(SD_LIB_DIR     "${SD_BUILD_DIR}/lib")

    if(EXISTS "${SD_LIB_DIR}/libstable-diffusion.a")
        message(STATUS "sd: using prebuilt static library")
        target_sources(nnserver PRIVATE sd_module.cpp)
        target_include_directories(nnserver PRIVATE "${SD_INCLUDE_DIR}")
        target_link_directories(nnserver PRIVATE "${SD_LIB_DIR}")
        target_link_libraries(nnserver PRIVATE
            -Wl,--start-group
            stable-diffusion ggml ggml-base ggml-cpu
            -Wl,--end-group
            -fopenmp)

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
    else()
        message(STATUS "sd: prebuilt lib not found — SD disabled")
    endif()

else()  # ── Linux ──────────────────────────────────────────────────────────
    target_sources(nnserver PRIVATE ${CMAKE_SOURCE_DIR}/src/network/nn_download_lnx.cpp)

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
