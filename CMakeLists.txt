cmake_minimum_required(VERSION 3.24)
project(MaiConv VERSION 0.1.0 LANGUAGES C CXX)

option(MAICONV_ENABLE_ASAN "Enable address sanitizer" OFF)
option(MAICONV_ENABLE_UBSAN "Enable undefined behavior sanitizer" OFF)
option(MAICONV_BUILD_TESTS "Build tests" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/CLI11 EXCLUDE_FROM_ALL)
add_subdirectory(third_party/tinyxml2 EXCLUDE_FROM_ALL)

if(MAICONV_BUILD_TESTS)
    add_subdirectory(third_party/Catch2 EXCLUDE_FROM_ALL)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Catch2/extras")
endif()

set(BUILD_CLI OFF CACHE BOOL "" FORCE)
set(BUILD_WINAMP OFF CACHE BOOL "" FORCE)
set(BUILD_FB2K OFF CACHE BOOL "" FORCE)
set(BUILD_XMPLAY OFF CACHE BOOL "" FORCE)
set(BUILD_AUDACIOUS OFF CACHE BOOL "" FORCE)
set(BUILD_V123 OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC OFF CACHE BOOL "" FORCE)

set(USE_MPEG OFF CACHE BOOL "" FORCE)
set(USE_VORBIS OFF CACHE BOOL "" FORCE)
set(USE_FFMPEG OFF CACHE BOOL "" FORCE)
set(USE_G7221 OFF CACHE BOOL "" FORCE)
set(USE_G719 OFF CACHE BOOL "" FORCE)
set(USE_ATRAC9 OFF CACHE BOOL "" FORCE)
set(USE_CELT OFF CACHE BOOL "" FORCE)
set(USE_SPEEX OFF CACHE BOOL "" FORCE)

add_subdirectory(third_party/vgmstream third_party/vgmstream-build EXCLUDE_FROM_ALL)

file(GLOB SHINE_SOURCES CONFIGURE_DEPENDS
    third_party/shine/src/lib/*.c
)
add_library(shine STATIC ${SHINE_SOURCES})
target_include_directories(shine PUBLIC third_party/shine/src/lib)

add_library(MaiConv_core
    src/core/chart.cpp
    src/core/format.cpp
    src/core/io.cpp
    src/core/ma2.cpp
    src/core/media.cpp
    src/core/minimp4_impl.c
    src/core/simai.cpp
    src/core/assets.cpp
)

target_include_directories(MaiConv_core PUBLIC include)
target_include_directories(MaiConv_core PRIVATE
    third_party/vgmstream/src
    third_party/shine/src/lib
    third_party/minimp4
)
target_link_libraries(MaiConv_core PUBLIC tinyxml2::tinyxml2)
target_link_libraries(MaiConv_core PRIVATE libvgmstream shine)



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
    )
    target_link_libraries(MaiConv_unit_tests
        PRIVATE
        MaiConv_core
        Catch2::Catch2WithMain
    )
    catch_discover_tests(MaiConv_unit_tests)
endif()

