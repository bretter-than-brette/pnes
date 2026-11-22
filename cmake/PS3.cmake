# CMAKE_TOOLCHAIN_FILE for TARGET_PLATFORM=ps3.
#
# ps3dev ships no CMake toolchain of its own -- PSL1GHT provides Makefile rules
# only -- so this project supplies one. docker/Dockerfile.ps3 builds ps3dev and
# exports PS3DEV/PSL1GHT, which the ps3-* presets assume are already set.
#
# Clang rather than ps3dev own GCC, for #embed: see the prxptr shim in
# src/ps3 for the one PSL1GHT header incompatibility that costs.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ppc64)

if(NOT DEFINED ENV{PS3DEV})
    message(FATAL_ERROR
        "PS3 build requires the PS3DEV env var (ps3dev toolchain root, e.g. "
        "/usr/local/ps3dev inside the pnes-ps3 Docker image -- see "
        "docker/Dockerfile.ps3 and the ps3-* services in compose.yml).")
endif()

set(PS3DEV "$ENV{PS3DEV}")
set(PS3_PPU_ROOT   "${PS3DEV}/ppu")
set(PS3_PPU_TRIPLE powerpc64-ps3-elf)
set(PS3_PPU_SYSROOT "${PS3_PPU_ROOT}/${PS3_PPU_TRIPLE}")
set(PS3_PPU_GXX    "${PS3_PPU_ROOT}/bin/${PS3_PPU_TRIPLE}-g++")

if(NOT EXISTS "${PS3_PPU_GXX}")
    message(FATAL_ERROR
        "PS3 build expected the ps3dev PPU driver at ${PS3_PPU_GXX} but it is "
        "not there. PS3DEV is set to '${PS3DEV}' -- is that really a ps3dev "
        "install built by ps3toolchain (see docker/Dockerfile.ps3)?")
endif()

file(GLOB PS3_GCC_LIBDIRS "${PS3_PPU_ROOT}/lib/gcc/${PS3_PPU_TRIPLE}/*")
list(FILTER PS3_GCC_LIBDIRS EXCLUDE REGEX "\\.(a|o|so|la)$")
if(NOT PS3_GCC_LIBDIRS)
    message(FATAL_ERROR
        "PS3 build could not find a libgcc directory under "
        "${PS3_PPU_ROOT}/lib/gcc/${PS3_PPU_TRIPLE}/ -- the ps3dev install at "
        "'${PS3DEV}' looks incomplete.")
endif()
list(SORT PS3_GCC_LIBDIRS COMPARE NATURAL ORDER DESCENDING)
list(GET PS3_GCC_LIBDIRS 0 PS3_GCC_LIBDIR)
get_filename_component(PS3_GCC_VERSION "${PS3_GCC_LIBDIR}" NAME)

set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)

set(PS3_CLANG_TARGET powerpc64-unknown-elf)
set(CMAKE_C_COMPILER_TARGET   ${PS3_CLANG_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${PS3_CLANG_TARGET})
set(CMAKE_ASM_COMPILER_TARGET ${PS3_CLANG_TARGET})

file(GLOB PS3_CXX_INCDIRS "${PS3_PPU_SYSROOT}/include/c++/*")
list(FILTER PS3_CXX_INCDIRS EXCLUDE REGEX "\\.(h|hpp|tcc)$")
if(NOT PS3_CXX_INCDIRS)
    message(FATAL_ERROR
        "PS3 build could not find libstdc++ headers under "
        "${PS3_PPU_SYSROOT}/include/c++/ -- the ps3dev install at '${PS3DEV}' "
        "looks incomplete (was ps3toolchain step 002 run?).")
endif()
list(SORT PS3_CXX_INCDIRS COMPARE NATURAL ORDER DESCENDING)
list(GET PS3_CXX_INCDIRS 0 PS3_CXX_INCDIR)

set(PS3_CLANG_FLAGS
    "--sysroot=${PS3_PPU_SYSROOT} -nostdlibinc -nostdinc++ -mlong-double-64 \
     -fno-use-init-array \
     -isystem ${PS3_CXX_INCDIR} \
     -isystem ${PS3_CXX_INCDIR}/${PS3_PPU_TRIPLE} \
     -isystem ${PS3_PPU_SYSROOT}/include \
     -isystem ${PS3_PPU_ROOT}/include")
string(REGEX REPLACE "[ \t\r\n]+" " " PS3_CLANG_FLAGS "${PS3_CLANG_FLAGS}")
set(CMAKE_C_FLAGS_INIT   "${PS3_CLANG_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${PS3_CLANG_FLAGS}")
set(CMAKE_ASM_FLAGS_INIT "${PS3_CLANG_FLAGS}")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_CXX_LINK_EXECUTABLE
    "${PS3_PPU_GXX} <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
set(CMAKE_C_LINK_EXECUTABLE
    "${PS3_PPU_ROOT}/bin/${PS3_PPU_TRIPLE}-gcc <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")

set(CMAKE_AR     "${PS3_PPU_ROOT}/bin/${PS3_PPU_TRIPLE}-ar")
set(CMAKE_RANLIB "${PS3_PPU_ROOT}/bin/${PS3_PPU_TRIPLE}-ranlib")

set(CMAKE_FIND_ROOT_PATH "${PS3DEV}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
