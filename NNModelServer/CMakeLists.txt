# ── nnserver (standalone matting inference server) ─────────────────────────────
set(NNSERVER_VERSION  "1.21.0" CACHE STRING "ONNX Runtime version")
set(NNSERVER_SOURCES
    nnserver.cpp
    nn_onnx.cpp
    matte_module.cpp
    sd_module.cpp
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

# ONNX Runtime for Windows (prebuilt binaries)
if(WIN32)
    set(ORT_ROOT "${CMAKE_BINARY_DIR}/onnxruntime" CACHE PATH "ONNX Runtime directory")
    set(ORT_INCLUDE_DIR "${ORT_ROOT}/include")
    set(ORT_LIB_DIR     "${ORT_ROOT}/lib")
    set(ORT_BIN_DIR     "${ORT_LIB_DIR}")

    if(NOT EXISTS "${ORT_INCLUDE_DIR}/onnxruntime_c_api.h")
        message(STATUS "Downloading ONNX Runtime ${NNSERVER_VERSION}...")
        set(_ort_url "https://github.com/microsoft/onnxruntime/releases/download/v${NNSERVER_VERSION}/onnxruntime-win-x64-${NNSERVER_VERSION}.zip")
        set(_ort_zip "${CMAKE_BINARY_DIR}/onnxruntime.zip")
        file(DOWNLOAD "${_ort_url}" "${_ort_zip}" SHOW_PROGRESS)
        file(ARCHIVE_EXTRACT INPUT "${_ort_zip}" DESTINATION "${ORT_ROOT}-tmp")
        file(GLOB _ort_dirs "${ORT_ROOT}-tmp/*")
        list(LENGTH _ort_dirs _ort_n)
        if(_ort_n EQUAL 1)
            file(RENAME "${_ort_dirs}" "${ORT_ROOT}")
        else()
            file(RENAME "${ORT_ROOT}-tmp" "${ORT_ROOT}")
        endif()
        file(REMOVE "${_ort_zip}")
        file(REMOVE_RECURSE "${ORT_ROOT}-tmp")
    endif()

    if(MINGW)
        set(_ort_header "${ORT_INCLUDE_DIR}/onnxruntime_c_api.h")
        file(READ "${_ort_header}" _ort_hdr)
        string(REPLACE "#define ORT_API_CALL _stdcall" "#define ORT_API_CALL" _ort_hdr "${_ort_hdr}")
        string(REPLACE "#define NO_EXCEPTION noexcept" "#define NO_EXCEPTION" _ort_hdr "${_ort_hdr}")
        file(WRITE "${_ort_header}" "${_ort_hdr}")
    endif()

    add_executable(nnserver ${NNSERVER_SOURCES})
    target_sources(nnserver PRIVATE ${CMAKE_SOURCE_DIR}/src/network/nn_download_win.cpp)

    target_include_directories(nnserver PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        "${STB_DIR}"
        "${ORT_INCLUDE_DIR}")

    target_link_directories(nnserver PRIVATE "${ORT_LIB_DIR}")
    target_link_libraries(nnserver PRIVATE onnxruntime ws2_32 winhttp)

    add_custom_command(TARGET nnserver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ORT_BIN_DIR}/onnxruntime.dll"
            "$<TARGET_FILE_DIR:nnserver>/"
        COMMENT "Copying onnxruntime.dll")

    add_custom_command(TARGET nnserver POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/model_url.txt"
            "$<TARGET_FILE_DIR:nnserver>/model_url.txt"
        COMMENT "Copying model_url.txt")

    # ── stable-diffusion.cpp (prebuilt static library) ─────────────────
    # Build once: run tools/build_sd.sh
    # Artifacts land in ${CMAKE_CURRENT_SOURCE_DIR}/sd-build/
    set(SD_BUILD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sd-build")
    set(SD_INCLUDE_DIR "${SD_BUILD_DIR}/include")
    set(SD_LIB_DIR     "${SD_BUILD_DIR}/lib")

    if(EXISTS "${SD_LIB_DIR}/libstable-diffusion.a")
        message(STATUS "sd: using prebuilt static library")
        target_include_directories(nnserver PRIVATE "${SD_INCLUDE_DIR}")
        target_link_directories(nnserver PRIVATE "${SD_LIB_DIR}")
        target_link_libraries(nnserver PRIVATE
            -Wl,--start-group
            stable-diffusion ggml ggml-base ggml-cpu
            -Wl,--end-group)
        find_package(OpenMP)
        if(OpenMP_CXX_FOUND)
            target_link_libraries(nnserver PRIVATE OpenMP::OpenMP_CXX)
        endif()
    else()
        message(WARNING "sd: prebuilt lib not found at ${SD_LIB_DIR}"
                        " — run tools/build_sd.sh to enable SD")
    endif()
endif()
