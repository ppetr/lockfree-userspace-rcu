# Usage: -DCMAKE_TOOLCHAIN_FILE=clang-toolchain.cmake
set(CMAKE_C_COMPILER             "/usr/bin/clang-11")
set(CMAKE_C_FLAGS                "-Wall")
set(CMAKE_C_FLAGS_DEBUG          "-g")
set(CMAKE_C_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
set(CMAKE_C_FLAGS_RELEASE        "-O3 -DNDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g")

set(CMAKE_CXX_COMPILER             "/usr/bin/clang++-11")
set(CMAKE_CXX_FLAGS                "-Wall")
set(CMAKE_CXX_FLAGS_DEBUG          "-g")
set(CMAKE_CXX_FLAGS_MINSIZEREL     "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE        "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

set(LLVMAR_EXECUTABLE "/usr/bin/llvm-ar-11")
set(LLVMNM_EXECUTABLE "/usr/bin/llvm-nm-11")
set(LLVMRANLIB_EXECUTABLE "/usr/bin/llvm-ranlib-11")

set(CMAKE_OBJDUMP "/usr/bin/llvm-objdump-11")
set(CMAKE_EXE_LINKER_FLAGS "-fuse-ld=lld")

set(RUN_HAVE_STD_REGEX 0)
