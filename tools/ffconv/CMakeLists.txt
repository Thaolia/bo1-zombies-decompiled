# ffconv: retail PC fastfiles -> IWffs zones (docs/PORTING.md, phase 2). A 32-bit Windows tool like the retail
# game, built from WSL with MinGW (tools/ffconv/CMakePresets.json). Needs python3 with the clang bindings
# (python3-clang) to generate the layout tables, and the June 2010 DirectX SDK headers (-DDXSDK_DIR).
cmake_minimum_required(VERSION 3.21)

find_program(FFCONV_CCACHE ccache)
if(FFCONV_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER "${FFCONV_CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${FFCONV_CCACHE}")
endif()

project(ffconv C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "ffconv walks retail zones with the engine's 32-bit structs: build it for a 32-bit target")
endif()

set(CMAKE_CXX_STANDARD 20)
set(REPO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
cmake_path(NORMAL_PATH REPO_DIR)
set(SRC_DIR "${REPO_DIR}/src")
set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/gen")
set(DXSDK_DIR "/mnt/c/SDKs/DXSDK_Jun10/DXSDK" CACHE PATH "June 2010 DirectX SDK root")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_command(
    OUTPUT "${GEN_DIR}/ff_layouts.gen.h"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GEN_DIR}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/gen/layouts.py" --out "${GEN_DIR}/ff_layouts.gen.h"
            --dxsdk "${DXSDK_DIR}" --depfile "${GEN_DIR}/ff_layouts.gen.d"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gen/layouts.py" "${SRC_DIR}/database/db_load.cpp"
    DEPFILE "${GEN_DIR}/ff_layouts.gen.d"
    COMMENT "Generating the 32/64-bit layout tables")
add_custom_command(
    OUTPUT "${GEN_DIR}/db_load_ffconv.cpp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${GEN_DIR}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/gen/transform_db_load.py"
            "${SRC_DIR}/database/db_load.cpp" "${GEN_DIR}/db_load_ffconv.cpp"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gen/transform_db_load.py" "${SRC_DIR}/database/db_load.cpp"
    COMMENT "Hooking ffconv's copy of db_load.cpp")

file(GLOB ZLIB_SOURCES "${SRC_DIR}/zlib/*.c")
set(MOJOSHADER_DIR "${REPO_DIR}/third_party/mojoshader")
set(MOJOSHADER_SOURCES
    "${MOJOSHADER_DIR}/mojoshader.c"
    "${MOJOSHADER_DIR}/mojoshader_common.c"
    "${MOJOSHADER_DIR}/profiles/mojoshader_profile_common.c"
    "${MOJOSHADER_DIR}/profiles/mojoshader_profile_glsl.c")
# Only the GLSL profile is vendored (third_party/mojoshader); the others are compiled out.
set_source_files_properties(${MOJOSHADER_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "SUPPORT_PROFILE_D3D=0;SUPPORT_PROFILE_BYTECODE=0;SUPPORT_PROFILE_HLSL=0;SUPPORT_PROFILE_GLSL120=0;SUPPORT_PROFILE_GLSLES=0;SUPPORT_PROFILE_GLSLES3=0;SUPPORT_PROFILE_ARB1=0;SUPPORT_PROFILE_ARB1_NV=0;SUPPORT_PROFILE_METAL=0;SUPPORT_PROFILE_SPIRV=0;SUPPORT_PROFILE_GLSPIRV=0"
    COMPILE_OPTIONS "-w")
add_executable(ffconv
    src/ffconv.cpp
    src/ff_glsl.cpp
    src/ff_shaders.cpp
    src/ff_stream.cpp
    src/ff_stubs.cpp
    "${GEN_DIR}/db_load_ffconv.cpp"
    "${GEN_DIR}/ff_layouts.gen.h"
    ${ZLIB_SOURCES}
    ${MOJOSHADER_SOURCES})

# src/database: the copy of db_load.cpp includes its neighbours by relative name.
target_include_directories(ffconv PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "${GEN_DIR}" "${REPO_DIR}" "${SRC_DIR}"
    "${SRC_DIR}/libs" "${SRC_DIR}/database" "${MOJOSHADER_DIR}")
# The same dialect and defines as the engine's GNU builds (and as layouts.py's parse).
target_compile_definitions(ffconv PRIVATE WIN32 _CONSOLE BO1_MP BO1_OFFLINE)
target_compile_options(ffconv PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-include "${SRC_DIR}/platform/compat.h">
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-narrowing>
    $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-c++11-narrowing>
    -mms-bitfields
    "SHELL:-idirafter \"${DXSDK_DIR}/Include\"")
# Runs on any Windows PC without MinGW's DLLs; large zones need more than 2 GB of address space.
target_link_options(ffconv PRIVATE -static -Wl,--large-address-aware)
