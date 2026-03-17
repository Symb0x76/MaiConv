cmake_minimum_required(VERSION 3.24)
project(MaiConv VERSION 0.1.0 LANGUAGES C CXX)

option(MAICONV_ENABLE_ASAN "Enable address sanitizer" OFF)
option(MAICONV_ENABLE_UBSAN "Enable undefined behavior sanitizer" OFF)
option(MAICONV_BUILD_TESTS "Build tests" ON)
option(MAICONV_USE_CCACHE "Auto-detect and use ccache compiler launcher" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands for tooling" FORCE)

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

find_package(Git QUIET)
find_program(MAICONV_CCACHE_PROGRAM ccache)

set(MAICONV_BUILD_VERSION "${PROJECT_VERSION}")
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --dirty --always
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE MAICONV_GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(MAICONV_GIT_DESCRIBE)
        set(MAICONV_BUILD_VERSION "${MAICONV_GIT_DESCRIBE}")
    endif()
endif()
message(STATUS "MaiConv build version: ${MAICONV_BUILD_VERSION}")

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
if(MAICONV_BUILD_TESTS)
    maiconv_ensure_submodule("third_party/Catch2" "third_party/Catch2/CMakeLists.txt")
endif()

set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/CLI11 EXCLUDE_FROM_ALL)
add_subdirectory(third_party/tinyxml2 EXCLUDE_FROM_ALL)

if(MAICONV_BUILD_TESTS)
    set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
    set(CATCH_INSTALL_EXTRAS ON CACHE BOOL "" FORCE)
    set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)
    add_subdirectory(third_party/Catch2 EXCLUDE_FROM_ALL)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Catch2/extras")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

set(MAICONV_UABE_LIBCOMPRESSION_SOURCES
    src/uabe/libCompression/lz4.c
    src/uabe/libCompression/lz4frame.c
    src/uabe/libCompression/lz4hc.c
    src/uabe/libCompression/xxhash.c
    src/uabe/libCompression/lz4dec.cpp
    src/uabe/libCompression/lz4enc.cpp
    src/uabe/libCompression/LzFind.c
    src/uabe/libCompression/LzFindMt.c
    src/uabe/libCompression/LzmaDec.c
    src/uabe/libCompression/LzmaEnc.c
    src/uabe/libCompression/Threads.c
)

add_library(maiconv_uabe_libCompression STATIC
    ${MAICONV_UABE_LIBCOMPRESSION_SOURCES}
)
target_include_directories(maiconv_uabe_libCompression PUBLIC include)
target_include_directories(maiconv_uabe_libCompression PRIVATE src/uabe/libCompression)
if(NOT WIN32)
    target_link_libraries(maiconv_uabe_libCompression PUBLIC Threads::Threads)
endif()

add_library(maiconv_uabe_libStringConverter STATIC
    src/uabe/libStringConverter/convert.cpp
)
target_include_directories(maiconv_uabe_libStringConverter PUBLIC include)
target_include_directories(maiconv_uabe_libStringConverter PRIVATE src/uabe/libStringConverter)

add_library(maiconv_uabe_assetstools STATIC
    src/uabe/AssetsTools/AssetBundleFileFormat.cpp
    src/uabe/AssetsTools/AssetBundleFileTable.cpp
    src/uabe/AssetsTools/AssetsFileFormat.cpp
    src/uabe/AssetsTools/AssetsFileReader.cpp
    src/uabe/AssetsTools/AssetsFileTable.cpp
    src/uabe/AssetsTools/AssetsReplacer.cpp
    src/uabe/AssetsTools/AssetTypeClass.cpp
    src/uabe/AssetsTools/BundleReplacer.cpp
    src/uabe/AssetsTools/ClassDatabaseFile.cpp
)
target_include_directories(maiconv_uabe_assetstools PUBLIC include)
target_include_directories(maiconv_uabe_assetstools PRIVATE src/uabe/AssetsTools)
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
    src/core/media_audio.cpp
    src/core/media_cover.cpp
    src/core/media_video.cpp
    src/core/unity_assetbundle.cpp
    src/core/simai_tokenizer.cpp
    src/core/simai_parser.cpp
    src/core/simai_compiler.cpp
    src/core/assets.cpp
    src/uabe/Texture/lodepng.cpp
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|AppleClang")
    set_source_files_properties(
        src/uabe/Texture/lodepng.cpp
        PROPERTIES
        COMPILE_OPTIONS "-w"
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(
        src/uabe/Texture/lodepng.cpp
        PROPERTIES
        COMPILE_OPTIONS "-w"
    )
elseif(MSVC)
    set_source_files_properties(
        src/uabe/Texture/lodepng.cpp
        PROPERTIES
        COMPILE_OPTIONS "/w"
    )
endif()

target_include_directories(MaiConv_core PUBLIC include)
target_include_directories(MaiConv_core PRIVATE
    third_party
)
target_compile_definitions(MaiConv_core PRIVATE
    MAICONV_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    MAICONV_BUILD_VERSION="${MAICONV_BUILD_VERSION}"
    ASSETSTOOLS_IMPORTSTATIC
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _CRT_NONSTDC_NO_DEPRECATE
)
target_link_libraries(MaiConv_core PUBLIC tinyxml2::tinyxml2)
target_link_libraries(MaiConv_core PRIVATE maiconv_uabe_assetstools)

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
