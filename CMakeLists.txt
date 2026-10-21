cmake_minimum_required(VERSION 3.16)

# ccache wraps every compile when it is installed. The Visual Studio generator ignores compiler launchers, so the
# win32-msvc presets build without it.
find_program(BO1_CCACHE ccache)
if(BO1_CCACHE AND NOT CMAKE_GENERATOR MATCHES "^Visual Studio")
    set(CMAKE_C_COMPILER_LAUNCHER "${BO1_CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${BO1_CCACHE}")
endif()

project(BO1Zombies)

set(BIN_NAME "BO1Zombies")

set(CMAKE_CXX_STANDARD 20)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(TL_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/tl")
set(DW_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/DemonWare")

# BO1_PLATFORM picks the OS layer: win32 (the upstream build), posix (Linux desktop) or switch (devkitA64 + libnx).
# Only win32 builds today; the other two are filled in by the Switch port phases (docs/PORTING.md).
if(NINTENDO_SWITCH)
    set(BO1_PLATFORM "switch")
elseif(WIN32)
    set(BO1_PLATFORM "win32")
else()
    set(BO1_PLATFORM "posix")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(BO1_ARCH_64 TRUE)
else()
    set(BO1_ARCH_64 FALSE)
endif()
message(STATUS "BO1: platform=${BO1_PLATFORM} 64-bit=${BO1_ARCH_64} compiler=${CMAKE_CXX_COMPILER_ID}")

include(cmake_files.cmake)

# BO1_OFFLINE builds the solo game without the online services (DemonWare, voice chat) and without the Windows-only
# binary SDKs (Steam, Bink, NVAPI, libvpx). src/platform/stubs/ answers for them the way they behave on a PC with no
# Steam client, no NVIDIA driver and no voice sound device, paths the engine already handles. The movie capture
# (mjpeg, on libvpx), the monkey test harness and Tracy go too. It is always on outside win32; on win32 the
# win32-msvc-offline preset turns it on so the stubs can be played against the reference build.
if(BO1_PLATFORM STREQUAL "win32")
    option(BO1_OFFLINE "Build without online services and Windows-only binary SDKs" OFF)
else()
    set(BO1_OFFLINE ON)
endif()
if(BO1_OFFLINE)
    set_source_files_properties(
        ${DW_DEMONWAREROOT_FILES} ${DW_BDCORE_FILES} ${DW_BDPLATFORM_FILES} ${DW_FILES} ${GROUPVOICE_FILES}
        ${BINKLIB_FILES} ${VPX_FILES} ${MJPEG_FILES} ${MONKEY_FILES} ${TRACY_FILES}
        PROPERTIES HEADER_FILE_ONLY ON)
    target_sources(BO1Zombies PRIVATE ${PLATFORM_STUBS_FILES})
    # The stubs define the Steam and Bink entry points in the exe: their headers must not declare them dllimport.
    target_compile_definitions(BO1Zombies PUBLIC BO1_OFFLINE STEAM_API_NODLL __RADINEXE__)
endif()

# GCC and clang (not clang-cl) get the MSVC-dialect shims in front of every file.
# Narrowing in brace initialisers is only a warning on MSVC, which converts the value; GCC and clang make it an error
# and convert the same way once it is allowed.
if(NOT MSVC)
    target_compile_options(BO1Zombies PRIVATE -include "${SRC_DIR}/platform/compat.h"
        $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-Wno-narrowing>
        $<$<COMPILE_LANG_AND_ID:CXX,Clang>:-Wno-c++11-narrowing>)
    # Without LTC_SOURCE, libtomcrypt's rand_prime.c calls libtommath's mp_read_unsigned_bin/mp_prime_is_prime through
    # implicit declarations, as the MSVC build does. GCC 14+ and clang 16+ reject those by default.
    set_source_files_properties("${SRC_DIR}/libs/libtomcrypt-1.17/src/math/rand_prime.c"
        PROPERTIES COMPILE_OPTIONS "-Wno-error=implicit-function-declaration")
endif()

# ----- Preprocessor Defines -----
# Common to both Debug and Release
target_compile_definitions(BO1Zombies PUBLIC
    BO1_MP
)
# Debug-only
target_compile_definitions(BO1Zombies PUBLIC $<$<CONFIG:Debug>:_DEBUG>)
if(NOT BO1_OFFLINE)
    target_compile_definitions(BO1Zombies PUBLIC
        $<$<CONFIG:Debug>:TRACY_ENABLE>
        $<$<CONFIG:Debug>:TRACY_ON_DEMAND>
    )
endif()
# Release-only
target_compile_definitions(BO1Zombies PUBLIC
    $<$<CONFIG:Release>:NDEBUG>
)

# ----- Include directories -----
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/libs")
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libtomcrypt-1.17/src/headers")
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libtommath-1.0")
target_include_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libvpx-1.5.0/include")

if(BO1_PLATFORM STREQUAL "win32")
    if(NOT BO1_ARCH_64)
        set(CMAKE_GENERATOR_PLATFORM "WIN32")
    endif()

    target_compile_definitions(BO1Zombies PUBLIC
        WIN32
        _CONSOLE
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_NO_DEPRECATED_NO_WARNINGS
    )

    # phys_gjk.cpp specific flag (experimental). /arch:IA32 only exists for 32-bit x86 MSVC.
    if(MSVC AND NOT BO1_ARCH_64)
        set_source_files_properties(
            "${SRC_DIR}/physics/phys_gjk.cpp"
            PROPERTIES
            COMPILE_OPTIONS "/arch:IA32"
        )
    endif()

    # MSVC 19.43 (VS 17.13) hits an internal compiler error (C1001, p2/main.cpp line 255) on rb_draw3d.cpp at
    # /O2 /Ob2. Limiting inlining for that one file avoids it; other compilers keep the default flags.
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND MSVC_VERSION EQUAL 1943)
        set_source_files_properties(
            "${SRC_DIR}/gfx_d3d/rb_draw3d.cpp"
            PROPERTIES
            COMPILE_OPTIONS "$<$<NOT:$<CONFIG:Debug>>:/Ob1>"
        )
    endif()

    # DXSDK_DIR: the June 2010 DirectX SDK root (Include/, Lib/x86/, Lib/x64/). Defaults to its installer's path;
    # pass -DDXSDK_DIR=<dir> to use an extracted copy instead.
    set(DXSDK_DIR "C:/Program Files (x86)/Microsoft DirectX SDK (June 2010)" CACHE PATH "June 2010 DirectX SDK root")
    if(MSVC)
        target_include_directories(BO1Zombies PUBLIC "${DXSDK_DIR}/Include")
    else()
        # MinGW ships its own Windows/DirectX headers; the SDK only fills in the ones it lacks (dxerr.h, xapobase.h).
        target_compile_options(BO1Zombies PRIVATE "SHELL:-idirafter \"${DXSDK_DIR}/Include\"")
    endif()

    # ----- Library directories -----
    if(BO1_ARCH_64)
        target_link_directories(BO1Zombies PUBLIC "${DXSDK_DIR}/Lib/x64")
    else()
        target_link_directories(BO1Zombies PUBLIC "${DXSDK_DIR}/Lib/x86")
    endif()
    target_link_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/libs/libvpx-1.5.0")
    target_link_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/steam/redistributable_bin")
    target_link_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/binklib")
    target_link_directories(BO1Zombies PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/nvapi")

    if(MSVC)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT  /O2 /MP /W3 ${MSVC_WARNING_DISABLES} /permissive")
        set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} /MTd /Od /MP /W3 ${MSVC_WARNING_DISABLES} /permissive")
    endif()

    if (MSVC AND WIN32 AND NOT MSVC_VERSION VERSION_LESS 142)
        target_link_options(BO1Zombies PRIVATE $<$<CONFIG:Debug>:/INCREMENTAL>)
        target_compile_options(BO1Zombies PRIVATE $<$<CONFIG:Debug>:/ZI>)

        set(CMAKE_CXX_FLAGS_DEBUG           "${CMAKE_CXX_FLAGS_DEBUG} /SAFESEH:NO")
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG    "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /SAFESEH:NO")
        set(CMAKE_SHARED_LINKER_FLAGS_DEBUG "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} /SAFESEH:NO")
        set(CMAKE_MODULE_LINKER_FLAGS_DEBUG "${CMAKE_MODULE_LINKER_FLAGS_DEBUG} /SAFESEH:NO")
    endif()

    # ----- Links -----
    # The binary SDKs that src/platform/stubs/ replaces under BO1_OFFLINE
    set(BO1_SDK_LIB "$<NOT:$<BOOL:${BO1_OFFLINE}>>")
    target_link_libraries(BO1Zombies PUBLIC
        $<${BO1_SDK_LIB}:steam_api.lib>
        $<${BO1_SDK_LIB}:binkw32.lib>
        $<${BO1_SDK_LIB}:nvapi.lib>
        d3d9.lib
        ws2_32.lib
        dxguid.lib
        dsound.lib
        winmm.lib
        xinput.lib
        xapobase.lib
        dxerr.lib
        $<${BO1_SDK_LIB}:libvpx.lib>
        # June-2010 DirectX SDK libs (xapobase, dxerr) reference pre-VS2015 CRT
        # symbols (_vsnprintf, etc.) that the modern UCRT no longer exports.
        # legacy_stdio_definitions.lib provides shims so those static libs link.
        legacy_stdio_definitions.lib
    )
    # ----- Debug Links -----
    target_link_libraries(BO1Zombies PUBLIC $<$<CONFIG:Debug>:d3dx9d.lib>)
    # ----- Release Links -----
    target_link_libraries(BO1Zombies PUBLIC $<$<CONFIG:Release>:d3dx9.lib>)
    if(MSVC AND NOT BO1_ARCH_64)
        target_link_options(BO1Zombies PRIVATE /machine:x86)
    endif()

    # ----- Linker Options -----
    # /DYNAMICBASE:NO - Turning this off causes directx to crash "directx has encountered an unrecoverable error".
    # /LARGEADDRESSAWARE - for convenience (4gb)
    # /DEBUG - PDB for both configs
    if(MSVC)
        target_link_options(BO1Zombies PRIVATE /DYNAMICBASE:NO /LARGEADDRESSAWARE /DEBUG)
    endif()

    set_target_properties(BO1Zombies PROPERTIES WIN32_EXECUTABLE TRUE)

    # Set the VS debugger's working directory to the build-output dir for whichever config is active
    set_target_properties(BO1Zombies PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    )

    # Keep the build machine's folder out of the binary: source paths in asserts are trimmed to the project and the
    # PDB is referenced by file name only. /d1trimfile is MSVC-only (clang-cl rejects it).
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}" BO1_SOURCE_DIR_NATIVE)
        target_compile_options(BO1Zombies PRIVATE "/d1trimfile:${BO1_SOURCE_DIR_NATIVE}")
    endif()
    if(MSVC)
        target_link_options(BO1Zombies PRIVATE "/PDBALTPATH:%_PDB%")
    endif()
else()
    # posix / switch: the engine does not compile here yet. Phase 1 adds src/platform/compat.h and the
    # -fsyntax-only pass; see docs/PORTING.md.
    message(WARNING "BO1: the ${BO1_PLATFORM} build is a work in progress and is expected to fail (docs/PORTING.md)")
endif()

# Mods live apart from the base game: mods/<name>/ is copied next to the exe on every build and is only
# loaded with +set fs_game mods/<name> (see mods/sandbox/README.md), or several at once with +set fs_mods "a b c"
# (mods/_stack/maps/_modstack.gsc). Without fs_game / fs_mods nothing in it is read.
add_custom_target(copy_mods ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/mods" "${CMAKE_BINARY_DIR}/$<CONFIG>/mods"
    COMMENT "Copying mods/ to the run folder")

# Base-game data files the install does not carry (data/main/ -> main/ next to the exe), e.g. playlists_sp.info,
# the offline stand-in for the retail online WAD entry (see BG_SetZombieExternalDvars in src/bgame/bg_misc.cpp).
add_custom_target(copy_base_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data/main" "${CMAKE_BINARY_DIR}/$<CONFIG>/main"
    COMMENT "Copying data/main/ to the run folder")
