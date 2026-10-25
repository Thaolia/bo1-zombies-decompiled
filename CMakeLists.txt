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
# (mjpeg, on libvpx), the monkey test harness and Tracy go too. It is always on outside 32-bit win32 (the SDKs are
# 32-bit Windows libraries); there the win32-msvc-offline preset turns it on so the stubs can be played against the
# reference build.
if(BO1_PLATFORM STREQUAL "win32" AND NOT BO1_ARCH_64)
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
    # The decompiled code is x86 machine code written back as C: it reads objects through other types everywhere
    # (*(_DWORD *)&...), its signed arithmetic is the instructions' own (it wraps), and it may test a pointer it has
    # already dereferenced. MSVC never optimises on any of those; GCC and clang must not either.
    target_compile_options(BO1Zombies PRIVATE -fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks)
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
    # posix / switch: Win32 declarations come from our shim (src/platform/posix/include); the D3D9 ones are copied
    # from the user's DirectX SDK at configure time, so nothing from Microsoft is committed.
    set(DXSDK_DIR "/mnt/c/SDKs/DXSDK_Jun10/DXSDK" CACHE PATH "June 2010 DirectX SDK root (its Include/d3d9*.h are used)")
    foreach(BO1_DX_HEADER d3d9.h d3d9types.h d3d9caps.h)
        if(NOT EXISTS "${DXSDK_DIR}/Include/${BO1_DX_HEADER}")
            message(FATAL_ERROR "BO1: ${DXSDK_DIR}/Include/${BO1_DX_HEADER} not found; pass -DDXSDK_DIR=<June 2010 SDK root>")
        endif()
        configure_file("${DXSDK_DIR}/Include/${BO1_DX_HEADER}" "${CMAKE_BINARY_DIR}/dxsdk/${BO1_DX_HEADER}" COPYONLY)
    endforeach()
    target_include_directories(BO1Zombies PRIVATE
        "${SRC_DIR}/platform/posix/include"
        "${SRC_DIR}/platform/posix/include_cased"
        "${CMAKE_BINARY_DIR}/dxsdk")
    # Windows-only sources the posix/switch platform layer replaces (docs/PORTING.md, phase 4):
    # - win_syscon.cpp (the Win32 console window; posix_syscon.cpp is the terminal) and win_mini_dumper.cpp (dbghelp
    #   minidumps, token privileges; posix_main.cpp keeps its started flag);
    # - XAudio2: snd_driver_sdl2.cpp implements sound/snd_driver.h with a software mixer on an SDL2 device (RAM voices;
    #   streaming and reverb deferred). It falls back to 'nosnd' when no device opens (headless, no hardware).
    # The rest of win32/ and CubeMapGenLib build on the Win32 layer of src/platform/posix.
    set_source_files_properties(
        "${SRC_DIR}/win32/win_syscon.cpp"
        "${SRC_DIR}/win32/win_mini_dumper.cpp"
        "${SRC_DIR}/sound/snd_driver_xaudio2.cpp"
        "${SRC_DIR}/sound/snd_driver_xaudio2_dsp.cpp"
        PROPERTIES HEADER_FILE_ONLY ON)
    if(BO1_PLATFORM STREQUAL "posix")
        # Direct3D 9 is the frontend in src/gfx_d3d9emu on a backend of src/gfx_backend (docs/RENDERER.md): GL on an
        # SDL2 OpenGL context, else null; windows, displays and the D3D9 host are SDL2's.
        target_sources(BO1Zombies PRIVATE ${PLATFORM_POSIX_FILES} ${GFX_D3D9EMU_FILES} ${GFX_BACKEND_NULL_FILES}
                       ${GFX_BACKEND_GL_FILES})
        find_package(SDL2 REQUIRED)
        target_link_libraries(BO1Zombies PRIVATE SDL2::SDL2)
        # The GL backend loads its entry points from the context and only needs the Khronos header (libgl-dev).
        find_path(BO1_GLCOREARB_DIR GL/glcorearb.h REQUIRED)
        target_include_directories(BO1Zombies SYSTEM PRIVATE "${BO1_GLCOREARB_DIR}")
        # Only the dispatcher knows whether the target has the GL backend: a target-wide definition would rebuild
        # every file.
        set_target_properties(BO1Zombies PROPERTIES BO1_GB_GL ON)
        set_source_files_properties("${SRC_DIR}/gfx_backend/gb_backend.cpp" PROPERTIES
                                    COMPILE_DEFINITIONS "$<$<BOOL:$<TARGET_PROPERTY:BO1_GB_GL>>:BO1_GB_GL>")
        # D3DXSaveSurfaceToFileA's image encoders (third_party/stb), for that one file.
        set_source_files_properties("${SRC_DIR}/gfx_d3d9emu/d3dx9emu.cpp" PROPERTIES
                                    COMPILE_OPTIONS "-isystem;${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
        # The engine spells its includes for Windows' case-insensitive file systems (<Windows.h>); clang only warns
        # when the sources sit on one too (/mnt/c under WSL), where the spelling cannot matter.
        target_compile_options(BO1Zombies PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-nonportable-include-path>)
        find_package(Threads REQUIRED)
        target_link_libraries(BO1Zombies PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
        # The x86-64 build reads the win64 IWffs zones (src/database/db_ff64.h): before linking, ffconv's layout
        # generator checks that every streamed struct lays out here as it does there, and names the ones that do not.
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            set(BO1_LAYOUT_STAMP "${CMAKE_BINARY_DIR}/iwffs_layouts.stamp")
            add_custom_command(
                OUTPUT "${BO1_LAYOUT_STAMP}"
                COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/tools/ffconv/gen/layouts.py"
                        --check-target64 x86_64-linux-gnu --dxsdk "${DXSDK_DIR}" --posix-dxsdk "${CMAKE_BINARY_DIR}/dxsdk"
                        --stamp "${BO1_LAYOUT_STAMP}" --depfile "${BO1_LAYOUT_STAMP}.d"
                DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/ffconv/gen/layouts.py" "${SRC_DIR}/database/db_load.cpp"
                DEPFILE "${BO1_LAYOUT_STAMP}.d"
                COMMENT "Checking the IWffs struct layouts on this ABI")
            add_custom_target(iwffs_layout_check DEPENDS "${BO1_LAYOUT_STAMP}")
            add_dependencies(BO1Zombies iwffs_layout_check)
        endif()
    elseif(BO1_PLATFORM STREQUAL "switch")
        # Switch (devkitA64 + libnx) reuses the posix SDL2 + Win32-shim layer; the /proc and x86 deltas are
        # #if defined(__SWITCH__) in the shared files and the libnx entry/romfs live in src/platform/switch. Null
        # backend this session (docs/PORTING.md, phase 5): GL (EGL on nwindowGetDefault) and the .nro come next.
        target_sources(BO1Zombies PRIVATE ${PLATFORM_POSIX_FILES} ${PLATFORM_SWITCH_FILES}
                       ${GFX_D3D9EMU_FILES} ${GFX_BACKEND_NULL_FILES})
        # The desktop entry (posix_main.cpp) is replaced by the libnx one (src/platform/switch/switch_main.cpp).
        set_source_files_properties("${SRC_DIR}/platform/posix/posix_main.cpp" PROPERTIES HEADER_FILE_ONLY ON)
        # aarch64 has no x86 SSE header: src/platform/switch/include/xmmintrin.h forwards to sse2neon (NEON).
        target_include_directories(BO1Zombies PRIVATE "${SRC_DIR}/platform/switch/include")
        target_include_directories(BO1Zombies SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sse2neon")
        # newlib gates GNU extensions (fopencookie, ...) on _GNU_SOURCE, which must precede every header, so it goes
        # on the command line rather than in a source file (the forced -include compat.h would beat a #define).
        target_compile_definitions(BO1Zombies PRIVATE _GNU_SOURCE)
        # SDL2 from the devkitPro portlibs (its pkg-config pulls EGL/glapi/drm_nouveau/libnx/pthread).
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(BO1_SDL2 REQUIRED sdl2)
        target_include_directories(BO1Zombies SYSTEM PRIVATE ${BO1_SDL2_INCLUDE_DIRS})
        target_link_libraries(BO1Zombies PRIVATE ${BO1_SDL2_LDFLAGS})
        # D3DXSaveSurfaceToFileA's image encoders (third_party/stb), for that one file, as on posix.
        set_source_files_properties("${SRC_DIR}/gfx_d3d9emu/d3dx9emu.cpp" PROPERTIES
                                    COMPILE_OPTIONS "-isystem;${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
        # Package the linked ELF into a runnable .nro (nacp metadata + icon). The game data lives on the SD card
        # (sdmc:/switch/bo1zm/), not in romfs; the embedded romfs (switch/romfs) holds only a version file so that
        # romfsInit() succeeds -- a homebrew NRO with no romfs makes Ryujinx throw on OpenDataStorageByCurrentProcess.
        find_program(BO1_ELF2NRO elf2nro REQUIRED HINTS "$ENV{DEVKITPRO}/tools/bin")
        find_program(BO1_NACPTOOL nacptool REQUIRED HINTS "$ENV{DEVKITPRO}/tools/bin")
        find_program(BO1_PYTHON3 NAMES python3 python REQUIRED)
        # GCC 16's libgcc carries guarded GCS/SME/SVE instructions in its unwinder that never run on the Tegra X1
        # (Cortex-A57, ARMv8.0) but abort Ryujinx's ARM JIT when it translates the unwind path. Patch them to NOP on
        # the linked ELF before packaging (switch-homebrew.md); the script is idempotent and verifies every site.
        add_custom_command(TARGET BO1Zombies POST_BUILD
            COMMAND "${BO1_NACPTOOL}" --create "BO1 Zombies" "bo1-zombies-decompiled (Switch port)" "0.1"
                    "${CMAKE_BINARY_DIR}/BO1Zombies.nacp"
            COMMAND "${BO1_PYTHON3}" "${CMAKE_CURRENT_SOURCE_DIR}/switch/tools/patch_ryujinx_mrs.py"
                    "$<TARGET_FILE:BO1Zombies>"
                    "--objdump=$ENV{DEVKITPRO}/devkitA64/bin/aarch64-none-elf-objdump"
                    "--readelf=$ENV{DEVKITPRO}/devkitA64/bin/aarch64-none-elf-readelf" --apply
            COMMAND "${BO1_ELF2NRO}" "$<TARGET_FILE:BO1Zombies>" "${CMAKE_BINARY_DIR}/$<CONFIG>/BO1Zombies.nro"
                    "--icon=$ENV{DEVKITPRO}/libnx/default_icon.jpg" "--nacp=${CMAKE_BINARY_DIR}/BO1Zombies.nacp"
                    "--romfsdir=${CMAKE_CURRENT_SOURCE_DIR}/switch/romfs"
            COMMENT "Patching libgcc GCS/SME for Ryujinx, then packaging BO1Zombies.nro (data stays on the SD card)"
            VERBATIM)
    endif()
    # The run folder, as the Visual Studio generator makes it: the program next to the copied main/ and mods/.
    set_target_properties(BO1Zombies PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>")
    if(BO1_PLATFORM STREQUAL "posix")
        # The frontend's unit test, on the null backend (docs/PORTING.md, phase 4): ./d3d9emu_test in the build folder.
        add_executable(d3d9emu_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/gfx_d3d9emu/d3d9emu_test.cpp"
                       ${GFX_D3D9EMU_FILES} ${GFX_BACKEND_NULL_FILES})
        target_include_directories(d3d9emu_test PRIVATE "${SRC_DIR}" "${SRC_DIR}/platform/posix/include"
                                   "${SRC_DIR}/platform/posix/include_cased")
        target_include_directories(d3d9emu_test SYSTEM PRIVATE "${CMAKE_BINARY_DIR}/dxsdk")
        target_compile_options(d3d9emu_test PRIVATE -include "${SRC_DIR}/platform/compat.h" -Wall -Wextra
                               -Wno-unused-parameter -Wno-unknown-pragmas
                               $<$<CXX_COMPILER_ID:Clang>:-Wno-nonportable-include-path>)
        # The GL backend's contract test (docs/RENDERER.md): it draws with the backend alone and reads the pixels
        # back. SDL_VIDEODRIVER=offscreen ./gb_gl_test in the build folder.
        add_executable(gb_gl_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/gfx_backend/gb_gl_test.cpp"
                       ${GFX_BACKEND_NULL_FILES} ${GFX_BACKEND_GL_FILES})
        target_include_directories(gb_gl_test PRIVATE "${SRC_DIR}")
        target_include_directories(gb_gl_test SYSTEM PRIVATE "${BO1_GLCOREARB_DIR}")
        target_link_libraries(gb_gl_test PRIVATE SDL2::SDL2 Threads::Threads)
        target_compile_options(gb_gl_test PRIVATE -Wall -Wextra)
        set_target_properties(gb_gl_test PROPERTIES BO1_GB_GL ON)
        # The controller mapping test (docs/PORTING.md, phase 4b): the pure SDL->XInput map, no device. ./gamepad_test.
        add_executable(gamepad_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/gamepad_test.cpp")
        target_include_directories(gamepad_test PRIVATE "${SRC_DIR}" "${SRC_DIR}/platform/posix/include"
                                   "${SRC_DIR}/platform/posix/include_cased")
        target_link_libraries(gamepad_test PRIVATE SDL2::SDL2)
        target_compile_options(gamepad_test PRIVATE -Wall -Wextra
                               $<$<CXX_COMPILER_ID:Clang>:-Wno-nonportable-include-path>)
        # The MS-ADPCM decoder test (docs/PORTING.md, phase 4b): the pure decoder against ffmpeg's adpcm_ms. The build
        # step makes the vectors with ffmpeg; ./adpcm_test <adpcm.wav> <reference.s16le> in the build folder.
        add_executable(adpcm_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/adpcm_test.cpp"
                       "${SRC_DIR}/platform/posix/snd_adpcm.cpp")
        target_include_directories(adpcm_test PRIVATE "${SRC_DIR}")
        target_compile_options(adpcm_test PRIVATE -Wall -Wextra)
        # The software mixer's math (docs/PORTING.md, phase 4b): the voice send matrices and their application, against
        # a computed reference, no device. ./snd_mix_test [out.wav] in the build folder.
        add_executable(snd_mix_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/snd_mix_test.cpp"
                       "${SRC_DIR}/platform/posix/snd_mix.cpp")
        target_include_directories(snd_mix_test PRIVATE "${SRC_DIR}")
        target_link_libraries(snd_mix_test PRIVATE m)
        target_compile_options(snd_mix_test PRIVATE -Wall -Wextra)
        message(STATUS "BO1: posix renders through the D3D9 frontend on the GL backend (null in headless runs); sound is a software mixer on SDL2, RAM voices only (docs/PORTING.md, phase 4)")
    else()
        message(WARNING "BO1: the ${BO1_PLATFORM} build is a work in progress and is expected to fail (docs/PORTING.md)")
    endif()
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
