cmake_minimum_required(VERSION 3.24)
project(MaiConv VERSION 0.1.0 LANGUAGES C CXX)

option(MAICONV_ENABLE_ASAN "Enable address sanitizer" OFF)
option(MAICONV_ENABLE_UBSAN "Enable undefined behavior sanitizer" OFF)
option(MAICONV_BUILD_TESTS "Build tests" ON)
option(MAICONV_USE_CCACHE "Auto-detect and use ccache compiler launcher" ON)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for tooling" FORCE)

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

find_package(Git QUIET)
find_program(MAICONV_CCACHE_PROGRAM ccache)

if(MAICONV_USE_CCACHE AND MAICONV_CCACHE_PROGRAM)
    if(NOT DEFINED CMAKE_C_COMPILER_LAUNCHER OR CMAKE_C_COMPILER_LAUNCHER STREQUAL "")
        set(CMAKE_C_COMPILER_LAUNCHER "${MAICONV_CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
    endif()
    if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER STREQUAL "")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${MAICONV_CCACHE_PROGRAM}" CACHE STRING "CXX compiler launcher" FORCE)
    endif()
endif()

if(NOT MAICONV_USE_CCACHE)
    if(CMAKE_C_COMPILER_LAUNCHER STREQUAL "ccache" OR
        (MAICONV_CCACHE_PROGRAM AND CMAKE_C_COMPILER_LAUNCHER STREQUAL MAICONV_CCACHE_PROGRAM))
        set(CMAKE_C_COMPILER_LAUNCHER "" CACHE STRING "C compiler launcher" FORCE)
    endif()
    if(CMAKE_CXX_COMPILER_LAUNCHER STREQUAL "ccache" OR
        (MAICONV_CCACHE_PROGRAM AND CMAKE_CXX_COMPILER_LAUNCHER STREQUAL MAICONV_CCACHE_PROGRAM))
        set(CMAKE_CXX_COMPILER_LAUNCHER "" CACHE STRING "CXX compiler launcher" FORCE)
    endif()
endif()

if(CMAKE_C_COMPILER_LAUNCHER STREQUAL "ccache" AND NOT MAICONV_CCACHE_PROGRAM)
    message(WARNING "CMAKE_C_COMPILER_LAUNCHER is set to 'ccache' but ccache was not found; disabling launcher.")
    set(CMAKE_C_COMPILER_LAUNCHER "" CACHE STRING "C compiler launcher" FORCE)
endif()
if(CMAKE_CXX_COMPILER_LAUNCHER STREQUAL "ccache" AND NOT MAICONV_CCACHE_PROGRAM)
    message(WARNING "CMAKE_CXX_COMPILER_LAUNCHER is set to 'ccache' but ccache was not found; disabling launcher.")
    set(CMAKE_CXX_COMPILER_LAUNCHER "" CACHE STRING "CXX compiler launcher" FORCE)
endif()

function(maiconv_ensure_submodule submodule_path check_relpath)
    set(check_path "${CMAKE_CURRENT_SOURCE_DIR}/${check_relpath}")
    if(EXISTS "${check_path}")
        return()
    endif()

    if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        message(STATUS "Missing dependency ${submodule_path}; running git submodule update --init --recursive -- ${submodule_path}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" submodule update --init --recursive -- "${submodule_path}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE git_result
            OUTPUT_QUIET
            ERROR_VARIABLE git_error
        )
        if(NOT git_result EQUAL 0)
            string(STRIP "${git_error}" git_error)
            message(WARNING "git submodule update failed for ${submodule_path}: ${git_error}")
        endif()
    endif()

    if(NOT EXISTS "${check_path}")
        message(FATAL_ERROR
            "Required dependency '${submodule_path}' is missing.\n"
            "Run: git submodule update --init --recursive")
    endif()
endfunction()

maiconv_ensure_submodule("third_party/CLI11" "third_party/CLI11/CMakeLists.txt")
maiconv_ensure_submodule("third_party/tinyxml2" "third_party/tinyxml2/CMakeLists.txt")
maiconv_ensure_submodule("third_party/vgmstream" "third_party/vgmstream/CMakeLists.txt")
maiconv_ensure_submodule("third_party/shine" "third_party/shine/src/lib/layer3.h")

function(maiconv_patch_vgmstream_ffmpeg)
    set(ffmpeg_cmake "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vgmstream/cmake/dependencies/ffmpeg.cmake")
    if(NOT EXISTS "${ffmpeg_cmake}")
        message(WARNING "vgmstream ffmpeg.cmake not found: ${ffmpeg_cmake}")
        return()
    endif()

    file(READ "${ffmpeg_cmake}" ffmpeg_content)
    string(FIND "${ffmpeg_content}" "if(NOT APPLE)" ffmpeg_has_apple_guard)
    string(FIND "${ffmpeg_content}" "--extra-cflags=--static" ffmpeg_has_static_cflags)
    if(ffmpeg_has_static_cflags EQUAL -1)
        return()
    endif()
    if(NOT ffmpeg_has_apple_guard EQUAL -1)
        return()
    endif()

    set(ffmpeg_old_block
        "                --enable-swresample
                --extra-libs=-static
                --extra-cflags=--static
                --pkg-config-flags=--static
            )")
    set(ffmpeg_new_block
        "                --enable-swresample
            )
            if(NOT APPLE)
                list(APPEND FFMPEG_CONF_ARGS
                    --extra-libs=-static
                    --extra-cflags=--static
                    --pkg-config-flags=--static
                )
            endif()")

    string(FIND "${ffmpeg_content}" "${ffmpeg_old_block}" ffmpeg_patch_pos)
    if(ffmpeg_patch_pos EQUAL -1)
        message(STATUS "Skipped vgmstream ffmpeg auto-patch: expected block not found in ${ffmpeg_cmake}")
        return()
    endif()

    string(REPLACE "${ffmpeg_old_block}" "${ffmpeg_new_block}" ffmpeg_content "${ffmpeg_content}")
    file(WRITE "${ffmpeg_cmake}" "${ffmpeg_content}")
    message(STATUS "Applied local vgmstream ffmpeg compatibility patch")
endfunction()

set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/CLI11 EXCLUDE_FROM_ALL)
add_subdirectory(third_party/tinyxml2 EXCLUDE_FROM_ALL)

if(MAICONV_BUILD_TESTS)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS ON CACHE BOOL "" FORCE)
    set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
    )
    FetchContent_MakeAvailable(catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

include_directories(
    ${CMAKE_SOURCE_DIR}/third_party/vgmstream/src
    ${CMAKE_SOURCE_DIR}/third_party/shine/src/lib
)

set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_WINAMP OFF CACHE BOOL "" FORCE)
set(BUILD_FB2K OFF CACHE BOOL "" FORCE)
set(BUILD_XMPLAY OFF CACHE BOOL "" FORCE)
set(BUILD_AUDACIOUS OFF CACHE BOOL "" FORCE)
set(BUILD_V123 OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC OFF CACHE BOOL "" FORCE)

set(USE_MPEG OFF CACHE BOOL "Disable mpg123 path; rely on FFmpeg for MPEG-family decoding" FORCE)
set(USE_VORBIS OFF CACHE BOOL "" FORCE)
set(USE_FFMPEG ON CACHE BOOL "Enable FFmpeg support in bundled vgmstream" FORCE)
set(USE_G7221 OFF CACHE BOOL "" FORCE)
set(USE_G719 OFF CACHE BOOL "" FORCE)
set(USE_ATRAC9 OFF CACHE BOOL "" FORCE)
set(USE_CELT OFF CACHE BOOL "" FORCE)
set(USE_SPEEX OFF CACHE BOOL "" FORCE)

maiconv_patch_vgmstream_ffmpeg()

add_subdirectory(third_party/vgmstream third_party/vgmstream-build EXCLUDE_FROM_ALL)

file(GLOB SHINE_SOURCES CONFIGURE_DEPENDS
    third_party/shine/src/lib/*.c
)
add_library(shine STATIC ${SHINE_SOURCES})
target_include_directories(shine PUBLIC third_party/shine/src/lib)

set(MAICONV_UABE_LIBCOMPRESSION_SOURCES
    src/core/unity_uabe/libCompression/lz4.c
    src/core/unity_uabe/libCompression/lz4frame.c
    src/core/unity_uabe/libCompression/lz4hc.c
    src/core/unity_uabe/libCompression/xxhash.c
    src/core/unity_uabe/libCompression/lz4dec.cpp
    src/core/unity_uabe/libCompression/lz4enc.cpp
    src/core/unity_uabe/libCompression/LzFind.c
    src/core/unity_uabe/libCompression/LzFindMt.c
    src/core/unity_uabe/libCompression/LzmaDec.c
    src/core/unity_uabe/libCompression/LzmaEnc.c
    src/core/unity_uabe/libCompression/Threads.c
)

add_library(maiconv_uabe_libCompression STATIC
    ${MAICONV_UABE_LIBCOMPRESSION_SOURCES}
)
target_include_directories(maiconv_uabe_libCompression PUBLIC src/core/unity_uabe/libCompression)
if(NOT WIN32)
    target_link_libraries(maiconv_uabe_libCompression PUBLIC Threads::Threads)
endif()

add_library(maiconv_uabe_libStringConverter STATIC
    src/core/unity_uabe/libStringConverter/convert.cpp
)
target_include_directories(maiconv_uabe_libStringConverter PUBLIC src/core/unity_uabe/libStringConverter)

add_library(maiconv_uabe_assetstools STATIC
    src/core/unity_uabe/AssetsTools/AssetBundleFileFormat.cpp
    src/core/unity_uabe/AssetsTools/AssetBundleFileTable.cpp
    src/core/unity_uabe/AssetsTools/AssetsFileFormat.cpp
    src/core/unity_uabe/AssetsTools/AssetsFileReader.cpp
    src/core/unity_uabe/AssetsTools/AssetsFileTable.cpp
    src/core/unity_uabe/AssetsTools/AssetsReplacer.cpp
    src/core/unity_uabe/AssetsTools/AssetTypeClass.cpp
    src/core/unity_uabe/AssetsTools/BundleReplacer.cpp
    src/core/unity_uabe/AssetsTools/ClassDatabaseFile.cpp
)
target_include_directories(maiconv_uabe_assetstools PUBLIC src/core/unity_uabe/AssetsTools)
target_compile_definitions(maiconv_uabe_assetstools PUBLIC
    ASSETSTOOLS_IMPORTSTATIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_DEPRECATE
)
target_link_libraries(maiconv_uabe_assetstools PUBLIC maiconv_uabe_libCompression maiconv_uabe_libStringConverter)

add_library(MaiConv_core
    src/core/chart.cpp
    src/core/format.cpp
    src/core/io.cpp
    src/core/ma2.cpp
    src/core/media.cpp
    src/core/unity_assetbundle.cpp
    src/core/simai.cpp
    src/core/assets.cpp
    src/core/unity_uabe/Texture/lodepng.cpp
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    set_source_files_properties(
        src/core/unity_uabe/Texture/lodepng.cpp
        PROPERTIES
        COMPILE_OPTIONS "-w"
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(
        src/core/unity_uabe/Texture/lodepng.cpp
        PROPERTIES
        COMPILE_OPTIONS "-w"
    )
elseif(MSVC)
    set_source_files_properties(
        src/core/unity_uabe/Texture/lodepng.cpp
        PROPERTIES
        COMPILE_OPTIONS "/w"
    )
endif()

target_include_directories(MaiConv_core PUBLIC include)
target_include_directories(MaiConv_core PRIVATE
    third_party/vgmstream/src
    third_party/shine/src/lib
    src/core/unity_uabe/AssetsTools
    src/core/unity_uabe/Texture
)
target_compile_definitions(MaiConv_core PRIVATE
    MAICONV_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    ASSETSTOOLS_IMPORTSTATIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_DEPRECATE
)
target_link_libraries(MaiConv_core PUBLIC tinyxml2::tinyxml2)
target_link_libraries(MaiConv_core PRIVATE libvgmstream shine maiconv_uabe_assetstools)

if(WIN32)
    target_link_libraries(MaiConv_core PRIVATE d3d11 d3dcompiler)
endif()

if(MSVC)
    target_compile_options(MaiConv_core PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
    target_compile_options(MaiConv_core
        PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
    )
endif()

if(NOT MSVC)
    if(MAICONV_ENABLE_ASAN)
        target_compile_options(MaiConv_core PUBLIC -fsanitize=address)
        target_link_options(MaiConv_core PUBLIC -fsanitize=address)
    endif()
    if(MAICONV_ENABLE_UBSAN)
        target_compile_options(MaiConv_core PUBLIC -fsanitize=undefined)
        target_link_options(MaiConv_core PUBLIC -fsanitize=undefined)
    endif()
endif()

add_executable(MaiConv
    src/cli/main.cpp
)

target_link_libraries(MaiConv PRIVATE MaiConv_core CLI11::CLI11)

if(MAICONV_BUILD_TESTS)
    include(CTest)
    enable_testing()
    include(Catch)

    add_executable(MaiConv_unit_tests
        tests/unit/test_chart.cpp
        tests/unit/test_ma2.cpp
        tests/unit/test_media.cpp
        tests/unit/test_simai.cpp
        tests/unit/test_assets.cpp
        tests/unit/test_uabe_lz4.cpp
    )
    target_link_libraries(MaiConv_unit_tests
        PRIVATE
        MaiConv_core
        Catch2::Catch2WithMain
    )
    catch_discover_tests(MaiConv_unit_tests)
endif()

