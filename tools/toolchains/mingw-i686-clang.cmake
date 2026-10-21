# 32-bit clang in GNU mode against the MinGW headers and libstdc++: same purpose as mingw-i686-gcc.cmake, with
# clang's diagnostics (closest to devkitA64-free Linux/Switch clang builds).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET i686-w64-mingw32)
set(CMAKE_CXX_COMPILER_TARGET i686-w64-mingw32)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# The engine spells some Windows headers in mixed case (<Windows.h>); MinGW installs them lowercase.
set(CMAKE_C_FLAGS_INIT "-idirafter ${CMAKE_CURRENT_LIST_DIR}/mingw-include")
set(CMAKE_CXX_FLAGS_INIT "-idirafter ${CMAKE_CURRENT_LIST_DIR}/mingw-include")
