# Windows x86_64 binary with mingw-w64 (from the toolchain image). Not fully static: the Windows
# C runtime is a system DLL; libgcc and winpthreads are linked in.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_AR x86_64-w64-mingw32-gcc-ar)
set(CMAKE_RANLIB x86_64-w64-mingw32-gcc-ranlib)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
