# CMAKE_TOOLCHAIN_FILE for TARGET_PLATFORM=ps2.
#
# ps2dev ships no CMake toolchain of its own, so this project supplies one. The
# ps2dev/ps2dev image exports PS2DEV/PS2SDK/GSKIT and puts the cross-compiler on
# PATH; this file needs only those three.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR mips)

if(NOT DEFINED ENV{PS2DEV})
    message(FATAL_ERROR
        "PS2 build requires the PS2DEV env var (ps2dev toolchain root, e.g. "
        "/usr/local/ps2dev inside the pnes-ps2 Docker image -- see "
        "docker/Dockerfile.ps2 and the ps2-* services in compose.yml).")
endif()

set(PS2DEV "$ENV{PS2DEV}")
set(CMAKE_C_COMPILER   "${PS2DEV}/ee/bin/mips64r5900el-ps2-elf-gcc")
set(CMAKE_CXX_COMPILER "${PS2DEV}/ee/bin/mips64r5900el-ps2-elf-g++")
set(CMAKE_ASM_COMPILER "${PS2DEV}/ee/bin/mips64r5900el-ps2-elf-gcc")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH "${PS2DEV}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
