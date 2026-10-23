# 64-bit clang in GNU mode against the MinGW headers: the phase 3 diagnostics pass (tools/syntax_check.py). clang
# rejects a pointer cast to a smaller integer outright in C++, so every 32-bit pointer assumption shows up as an
# error, with the Windows and DirectX headers still there.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-w64-mingw32)
set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-mingw32)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# The engine spells some Windows headers in mixed case (<Windows.h>); MinGW installs them lowercase. An integer
# turned into a pointer is an error too: a pointer kept in a 32-bit integer comes back truncated.
set(CMAKE_C_FLAGS_INIT "-idirafter ${CMAKE_CURRENT_LIST_DIR}/mingw-include -Werror=int-to-pointer-cast -Werror=pointer-to-int-cast")
set(CMAKE_CXX_FLAGS_INIT "-idirafter ${CMAKE_CURRENT_LIST_DIR}/mingw-include -Werror=int-to-pointer-cast")
