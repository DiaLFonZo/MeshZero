# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.22)
project(meshzero VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(MESHZERO_SDL "Build the SDL desktop window backend (needs libsdl2-dev)" OFF)
option(MESHZERO_BUILD_UI "Build the LVGL user interface" ON)
set(MESHZERO_LVGL_VERSION "v9.3.0" CACHE STRING "LVGL release tag")

include(GNUInstallDirs)

# ---------------------------------------------------------------- protocol
file(GLOB MESHZERO_PROTO_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/proto/gen/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/proto/gen/meshtastic/*.c")
add_library(mz_proto STATIC
    ${MESHZERO_PROTO_SOURCES}
    third_party/nanopb/pb_common.c
    third_party/nanopb/pb_decode.c
    third_party/nanopb/pb_encode.c)
target_include_directories(mz_proto PUBLIC third_party/nanopb src/proto/gen)

add_library(mz_common STATIC
    src/common/history.cpp
    src/common/proto.cpp
    src/common/util.cpp)
target_include_directories(mz_common PUBLIC src)
target_link_libraries(mz_common PUBLIC mz_proto)
target_compile_options(mz_common PRIVATE -Wall -Wextra)

# ------------------------------------------------------------------ daemon
add_executable(meshzerod
    src/daemon/hw.cpp
    src/daemon/main.cpp)
target_link_libraries(meshzerod PRIVATE mz_common)
target_compile_options(meshzerod PRIVATE -Wall -Wextra)

# ---------------------------------------------------------------------- UI
if(MESHZERO_BUILD_UI)
    include(FetchContent)
    # SOURCE_SUBDIR names a directory without a CMakeLists.txt on purpose: the
    # sources are only downloaded, and compiled by the target below.
    FetchContent_Declare(lvgl_src
        URL "https://github.com/lvgl/lvgl/archive/refs/tags/${MESHZERO_LVGL_VERSION}.tar.gz"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR docs)
    FetchContent_MakeAvailable(lvgl_src)

    # Compile LVGL from its C sources directly so no optional backends, demos
    # or assembly variants are pulled in.
    file(GLOB_RECURSE MESHZERO_LVGL_SOURCES "${lvgl_src_SOURCE_DIR}/src/*.c")
    add_library(lvgl STATIC ${MESHZERO_LVGL_SOURCES})
    target_include_directories(lvgl PUBLIC "${lvgl_src_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/config")
    target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
    target_compile_options(lvgl PRIVATE -w)

    file(GLOB MESHZERO_UI_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/screens/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/fonts/*.c")
    add_executable(meshzero ${MESHZERO_UI_SOURCES})
    target_include_directories(meshzero PRIVATE src)
    target_link_libraries(meshzero PRIVATE mz_common lvgl m)
    target_compile_options(meshzero PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    set(MESHZERO_FONT_SOURCE_DIR "${lvgl_src_SOURCE_DIR}/scripts/built_in_font")
    target_compile_definitions(meshzero PRIVATE
        MESHZERO_VERSION="${PROJECT_VERSION}"
        MESHZERO_DEV_FONT_DIR="${MESHZERO_FONT_SOURCE_DIR}"
        MESHZERO_DEV_EMOJI_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")

    if(MESHZERO_SDL)
        find_package(SDL2 REQUIRED)
        target_compile_definitions(lvgl PUBLIC MESHZERO_SDL=1)
        target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
        target_link_libraries(meshzero PRIVATE ${SDL2_LIBRARIES})
    endif()
endif()

# ----------------------------------------------------------------- tests
option(MESHZERO_BUILD_TESTS "Build unit tests" ON)
if(MESHZERO_BUILD_TESTS)
    enable_testing()
    add_executable(mz_tests tests/test_common.cpp)
    target_link_libraries(mz_tests PRIVATE mz_common)
    add_test(NAME common COMMAND mz_tests)

    # End-to-end proxy test; needs the reference Python client (pip install meshtastic).
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import meshtastic"
            RESULT_VARIABLE _mz_no_meshtastic OUTPUT_QUIET ERROR_QUIET)
        if(_mz_no_meshtastic EQUAL 0)
            add_test(NAME proxy COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_proxy.py" "$<TARGET_FILE:meshzerod>")
        else()
            message(STATUS "Python package 'meshtastic' not found; skipping the proxy end-to-end test")
        endif()
    endif()
endif()

include(cmake/package.cmake OPTIONAL)
