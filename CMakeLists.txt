cmake_minimum_required(VERSION 3.21)

# Per-developer overrides (SDK paths, cache vars). Copy local.cmake.example to
# local.cmake and edit; local.cmake is gitignored.
include("${CMAKE_SOURCE_DIR}/local.cmake" OPTIONAL)

# REGION names the market a build is for, as a STRING reaching C++ as a string
# literal -- so it is NOT testable with #if, and code that must branch at
# preprocess time keys off TARGET_* instead. There is no framerate axis beside
# it any more: every backend measures the refresh at runtime, so one NES ROM
# covers NTSC/PAL/Dendy and one .dol covers 50Hz, 60Hz and PAL60.
#
# Not a local.cmake knob: it is derived from TARGET_PLATFORM and set()
# unconditionally after that include, so a value set there is overwritten. Which
# market a build is for is a property of the target, not of the machine.
set(REGION "US")

if(TARGET_PLATFORM STREQUAL "nes" AND NOT CMAKE_TOOLCHAIN_FILE)
    if(NOT LLVM_MOS_SDK)
        message(FATAL_ERROR
            "NES build requires the llvm-mos-sdk location, but LLVM_MOS_SDK is "
            "unset. Copy local.cmake.example to local.cmake and set it to your "
            "SDK install root, e.g.\n"
            "    set(LLVM_MOS_SDK \"/path/to/llvm-mos\" CACHE PATH \"\" FORCE)\n"
            "The SDK does NOT need to be on your PATH.")
    endif()
    set(CMAKE_TOOLCHAIN_FILE
        "${LLVM_MOS_SDK}/lib/cmake/llvm-mos-sdk/llvm-mos-toolchain.cmake")
    set(CMAKE_PREFIX_PATH "${LLVM_MOS_SDK}"
        CACHE PATH "llvm-mos-sdk root (derived from LLVM_MOS_SDK)" FORCE)
endif()

if(TARGET_PLATFORM STREQUAL "nes")
    if(NOT TOPLEVEL_LD)
        message(FATAL_ERROR
            "NES build requires a top-level linker script, but TOPLEVEL_LD is "
            "unset. Copy local.cmake.example to local.cmake and set it to "
            "your own project's top-level script, e.g.\n"
            "    set(TOPLEVEL_LD \"\${CMAKE_SOURCE_DIR}/demo/link.ld\" CACHE FILEPATH \"\" FORCE)\n"
            "That script should INCLUDE mmc3-helper.ld (src/nes/mappers) for "
            "the hardware/mechanical parts it doesn't need to reinvent.")
    endif()
    set(CMAKE_CXX_FLAGS "-T ${TOPLEVEL_LD} -L${CMAKE_SOURCE_DIR}/src/nes/mappers -Wno-unused-command-line-argument")

    set(LLVM_MOS_PLATFORM nes)
endif()

if(NOT PROJECT_NAME)
    set(PROJECT_NAME "My pnes Game")
endif()

set(GAME_NAME "${PROJECT_NAME}")

if(NOT ORIENTATION)
    set(ORIENTATION "LANDSCAPE")
endif()
if(NOT ORIENTATION STREQUAL "LANDSCAPE" AND NOT ORIENTATION STREQUAL "PORTRAIT")
    message(FATAL_ERROR
        "ORIENTATION must be LANDSCAPE or PORTRAIT, got: \"${ORIENTATION}\"")
endif()
add_compile_definitions(${ORIENTATION})
add_compile_definitions(PROJECT_NAME="${PROJECT_NAME}")

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=gnu++23>)

include(FetchContent)

set(STB_GIT_TAG "033efa1bcb750136ac98e997d53833b758e0089b"
        CACHE STRING "nothings/stb commit to fetch")

FetchContent_Declare(
        intsh
        GIT_REPOSITORY https://github.com/brette-0/intsh.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
)

FetchContent_Declare(
        tuple
        GIT_REPOSITORY https://github.com/brette-0/tuple.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  br0
)

FetchContent_Declare(
        consh
        GIT_REPOSITORY https://github.com/brette-0/consh.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
)

macro(_make_consh_available)
    FetchContent_MakeAvailable(consh)
    if(NOT TARGET consh_headers)
        add_library(consh_headers INTERFACE)
        target_include_directories(consh_headers INTERFACE ${consh_SOURCE_DIR})
        add_library(br0::consh ALIAS consh_headers)
    endif()
endmacro()

macro(_fixup_fetched_cxx_std_targets)
    if(TARGET intsh)
        set_property(TARGET intsh PROPERTY INTERFACE_COMPILE_FEATURES "")
    endif()
    if(TARGET br0_tuple)
        set_property(TARGET br0_tuple PROPERTY INTERFACE_COMPILE_FEATURES "")
    endif()
endmacro()

# uitk-export -- regenerates demo/gen/<scene>/<target>/ for every
# demo/ui/*.uis, one nested cmake+run per scene. The output directory is the
# .uis file's own basename, so a new scene needs no edit here.
#
# Runs at CONFIGURE time, deliberately: demo/gen/ is gitignored, so each
# platform branch's own file(GLOB) below has to see the regenerated files before
# it globs. CMake bakes a GLOB's file list into the build graph at configure
# time, so a build-time target that adds or removes a title.cpp leaves that
# graph pointing at a file that is not there yet -- which is exactly how this
# broke once NES stopped needing a title.cpp at all.
#
# Built through a nested cmake rather than add_subdirectory so it never goes
# through this project's own toolchain: a nested invocation does not inherit
# CMAKE_TOOLCHAIN_FILE, so uitk always configures against the host compiler.
# Placed ahead of every project() call so find_package(Qt) searches the host
# too. Without Qt it warns and uses whatever demo/gen/ already holds.
set(_HOST_ENV_CXX "$ENV{CXX}")
set(_HOST_ENV_CC "$ENV{CC}")
enable_language(CXX)
set(_MOS_ENV_CXX "$ENV{CXX}")
set(_MOS_ENV_CC "$ENV{CC}")

set(NESTED_HOST_CMAKE_ARGS -G "${CMAKE_GENERATOR}")
if(CMAKE_MAKE_PROGRAM)
    list(APPEND NESTED_HOST_CMAKE_ARGS -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM})
endif()

macro(clear_nested_build_dir_on_generator_change dir)
    if(EXISTS "${dir}/CMakeCache.txt")
        file(STRINGS "${dir}/CMakeCache.txt" _NESTED_CACHED_GENERATOR
             REGEX "^CMAKE_GENERATOR:INTERNAL=")
        if(NOT _NESTED_CACHED_GENERATOR STREQUAL
           "CMAKE_GENERATOR:INTERNAL=${CMAKE_GENERATOR}")
            file(REMOVE_RECURSE "${dir}")
        endif()
        unset(_NESTED_CACHED_GENERATOR)
    endif()
endmacro()
find_package(Qt6 QUIET COMPONENTS Widgets)
if(NOT Qt6Widgets_FOUND)
    find_package(Qt5 QUIET COMPONENTS Widgets)
endif()

if(Qt6Widgets_FOUND OR Qt5Widgets_FOUND)
    file(GLOB_RECURSE UITK_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/uitk/*.cpp)
    file(GLOB UITK_SCENE_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/ui/*.uis)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 ${UITK_SCENE_FILES}
                 ${UITK_SOURCES}
                 ${CMAKE_SOURCE_DIR}/tools/uitk/CMakeLists.txt)

    if(_HOST_ENV_CXX)
        set(ENV{CXX} "${_HOST_ENV_CXX}")
    else()
        unset(ENV{CXX})
    endif()
    if(_HOST_ENV_CC)
        set(ENV{CC} "${_HOST_ENV_CC}")
    else()
        unset(ENV{CC})
    endif()

    clear_nested_build_dir_on_generator_change(${CMAKE_BINARY_DIR}/uitk-build)
    execute_process(
            COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR}/tools/uitk -B ${CMAKE_BINARY_DIR}/uitk-build
                    ${NESTED_HOST_CMAKE_ARGS}
            RESULT_VARIABLE UITK_CONFIGURE_RESULT)
    if(NOT UITK_CONFIGURE_RESULT EQUAL 0)
        message(FATAL_ERROR "uitk: failed to configure tools/uitk (see output above).")
    endif()
    execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/uitk-build
            RESULT_VARIABLE UITK_BUILD_RESULT)
    if(NOT UITK_BUILD_RESULT EQUAL 0)
        message(FATAL_ERROR "uitk: failed to build (see output above).")
    endif()
    set(UITK_DUAL_SCREEN_TARGETS DS DSI 3DS)
    foreach(UITK_SCENE_FILE ${UITK_SCENE_FILES})
        get_filename_component(UITK_SCENE_NAME ${UITK_SCENE_FILE} NAME_WE)
        if(UITK_SCENE_NAME MATCHES "_ds$")
            continue()
        endif()
        execute_process(
                COMMAND ${CMAKE_BINARY_DIR}/uitk-build/uitk --export-all
                        --input ${UITK_SCENE_FILE}
                        --output ${CMAKE_SOURCE_DIR}/demo/gen/${UITK_SCENE_NAME}
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                RESULT_VARIABLE UITK_EXPORT_RESULT)
        if(NOT UITK_EXPORT_RESULT EQUAL 0)
            message(FATAL_ERROR "uitk: export failed for ${UITK_SCENE_FILE} (see output above).")
        endif()

        set(UITK_SCENE_DS_FILE ${CMAKE_SOURCE_DIR}/demo/ui/${UITK_SCENE_NAME}_ds.uis)
        if(EXISTS ${UITK_SCENE_DS_FILE})
            foreach(UITK_DS_TARGET ${UITK_DUAL_SCREEN_TARGETS})
                execute_process(
                        COMMAND ${CMAKE_BINARY_DIR}/uitk-build/uitk --export
                                --target ${UITK_DS_TARGET}
                                --name ${UITK_SCENE_NAME}
                                --input ${UITK_SCENE_DS_FILE}
                                --output ${CMAKE_SOURCE_DIR}/demo/gen/${UITK_SCENE_NAME}
                        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                        RESULT_VARIABLE UITK_DS_EXPORT_RESULT)
                if(NOT UITK_DS_EXPORT_RESULT EQUAL 0)
                    message(FATAL_ERROR
                            "uitk: export failed for ${UITK_SCENE_DS_FILE} "
                            "(target ${UITK_DS_TARGET}) (see output above).")
                endif()
            endforeach()
        endif()
    endforeach()

    if(_MOS_ENV_CXX)
        set(ENV{CXX} "${_MOS_ENV_CXX}")
    else()
        unset(ENV{CXX})
    endif()
    if(_MOS_ENV_CC)
        set(ENV{CC} "${_MOS_ENV_CC}")
    else()
        unset(ENV{CC})
    endif()
elseif(EXISTS ${CMAKE_SOURCE_DIR}/demo/gen/title AND EXISTS ${CMAKE_SOURCE_DIR}/demo/gen/gameOptions)
    message(STATUS
        "Qt Widgets not found; using whatever's already under demo/gen/ "
        "instead of regenerating it from demo/ui/*.uis (e.g. a "
        "downloaded demo-gen CI artifact, or an earlier Qt-equipped local "
        "export). Install Qt6 (or Qt5) to keep it in sync here after "
        "editing demo/ui/title.uis or demo/ui/gameOptions.uis locally.")
else()
    message(FATAL_ERROR
        "Qt Widgets not found, and demo/gen/title/ or demo/gen/gameOptions/ "
        "doesn't exist -- nothing to fall back to. Install Qt6 (or Qt5) so "
        "this configure step can generate them from demo/ui/*.uis, or "
        "populate demo/gen/ yourself first (e.g. download codegen-check's "
        "demo-gen CI artifact).")
endif()

file(GLOB_RECURSE BLOBMAN_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/blobman/*.cpp)
file(GLOB_RECURSE BLOBMAN_ART CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/chr/assets/*.chr)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${CMAKE_SOURCE_DIR}/demo/chr/config.toml
             ${BLOBMAN_ART}
             ${BLOBMAN_SOURCES}
             ${CMAKE_SOURCE_DIR}/tools/blobman/CMakeLists.txt)

if(_HOST_ENV_CXX)
    set(BLOBMAN_HOST_CXX "${_HOST_ENV_CXX}")
else()
    find_program(BLOBMAN_HOST_CXX NAMES c++ g++ clang++ NO_CMAKE_FIND_ROOT_PATH)
endif()

if(_HOST_ENV_CXX)
    set(ENV{CXX} "${_HOST_ENV_CXX}")
else()
    unset(ENV{CXX})
endif()
if(_HOST_ENV_CC)
    set(ENV{CC} "${_HOST_ENV_CC}")
else()
    unset(ENV{CC})
endif()

if(BLOBMAN_HOST_CXX)
    clear_nested_build_dir_on_generator_change(${CMAKE_BINARY_DIR}/blobman-build)
    execute_process(
            COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR}/tools/blobman
                                     -B ${CMAKE_BINARY_DIR}/blobman-build
                                     -DCMAKE_BUILD_TYPE=Release
                                     -DCMAKE_CXX_COMPILER=${BLOBMAN_HOST_CXX}
                                     ${NESTED_HOST_CMAKE_ARGS}
            RESULT_VARIABLE BLOBMAN_CONFIGURE_RESULT
            OUTPUT_QUIET)
else()
    set(BLOBMAN_CONFIGURE_RESULT 1)
endif()

if(BLOBMAN_CONFIGURE_RESULT EQUAL 0)
    execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/blobman-build
            RESULT_VARIABLE BLOBMAN_BUILD_RESULT
            OUTPUT_QUIET)
else()
    set(BLOBMAN_BUILD_RESULT 1)
endif()

if(BLOBMAN_HOST_CXX)
    set(BLOBMAN_FALLBACK_REASON "tools/blobman failed to build with ${BLOBMAN_HOST_CXX} (see the output above -- most often no system toml++ and no network to fetch it)")
else()
    set(BLOBMAN_FALLBACK_REASON "this image/host has no C++ compiler of its own, only a cross toolchain (expected on the PS2 build image, for one)")
endif()

if(BLOBMAN_BUILD_RESULT EQUAL 0)
    execute_process(
            COMMAND ${CMAKE_BINARY_DIR}/blobman-build/blobman
                    -i config.toml -o chr.bin -l cpp -v
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/demo/chr
            RESULT_VARIABLE BLOBMAN_PACK_RESULT)
    if(NOT BLOBMAN_PACK_RESULT EQUAL 0)
        message(FATAL_ERROR
            "blobman: packing demo/chr/config.toml failed (see output above). "
            "The manifest or the art under demo/chr/assets/ is the problem -- "
            "this is not a toolchain fallback case, so it is fatal.")
    endif()
elseif(EXISTS ${CMAKE_SOURCE_DIR}/demo/chr/chr.bin AND EXISTS ${CMAKE_SOURCE_DIR}/demo/chr/chr.h)
    message(STATUS
        "blobman: ${BLOBMAN_FALLBACK_REASON}; using the committed "
        "demo/chr/chr.bin + chr.h instead of repacking them from "
        "demo/chr/config.toml. That is fine for a cross build -- the pair is "
        "committed -- but edits to the manifest or to demo/chr/assets/ will "
        "NOT be picked up here. Repack on a host with a C++20 compiler, or let "
        "CI's blobman-check job catch the drift.")
else()
    message(FATAL_ERROR
        "blobman: ${BLOBMAN_FALLBACK_REASON}, and demo/chr/chr.bin or chr.h is "
        "missing -- nothing to fall back to. Either restore the committed pair "
        "or give this host a C++20 compiler and a toml++ (system package or "
        "network); nothing below can compile against a CHR ROM that is not "
        "there.")
endif()

# animator-export -- the same arrangement as uitk-export above, for
# demo/animations/*.anim. A separate target from the designer itself: no Qt, no
# display server, so CI can run it.
file(GLOB ANIMATOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/animator/*.cpp)
file(GLOB ANIMATOR_PROJECTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/animations/*.anim)
file(GLOB_RECURSE ANIMATOR_METASPRITES CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/demo/animations/*.msprite)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${ANIMATOR_PROJECTS}
             ${ANIMATOR_METASPRITES}
             ${ANIMATOR_SOURCES}
             ${CMAKE_SOURCE_DIR}/tools/animator/CMakeLists.txt)

if(BLOBMAN_HOST_CXX)
    clear_nested_build_dir_on_generator_change(${CMAKE_BINARY_DIR}/animator-build)
    execute_process(
            COMMAND ${CMAKE_COMMAND} -S ${CMAKE_SOURCE_DIR}/tools/animator
                                     -B ${CMAKE_BINARY_DIR}/animator-build
                                     -DCMAKE_BUILD_TYPE=Release
                                     -DCMAKE_CXX_COMPILER=${BLOBMAN_HOST_CXX}
                                     -DANIMATOR_GUI=OFF
                                     ${NESTED_HOST_CMAKE_ARGS}
            RESULT_VARIABLE ANIMATOR_CONFIGURE_RESULT
            OUTPUT_QUIET)
else()
    set(ANIMATOR_CONFIGURE_RESULT 1)
endif()

if(ANIMATOR_CONFIGURE_RESULT EQUAL 0)
    execute_process(
            COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}/animator-build
                                     --target animator-export
            RESULT_VARIABLE ANIMATOR_BUILD_RESULT
            OUTPUT_QUIET)
else()
    set(ANIMATOR_BUILD_RESULT 1)
endif()

if(BLOBMAN_HOST_CXX)
    set(ANIMATOR_FALLBACK_REASON "tools/animator's exporter failed to build with ${BLOBMAN_HOST_CXX} (see the output above -- most often no system toml++ and no network to fetch it)")
else()
    set(ANIMATOR_FALLBACK_REASON "this image/host has no C++ compiler of its own, only a cross toolchain (expected on the PS2 build image, for one)")
endif()

file(GLOB ANIMATOR_COMMITTED_HEADERS ${CMAKE_SOURCE_DIR}/demo/animations/gen/*.hpp)

if(ANIMATOR_BUILD_RESULT EQUAL 0)
    foreach(ANIMATOR_PROJECT ${ANIMATOR_PROJECTS})
        file(RELATIVE_PATH ANIMATOR_PROJECT_RELATIVE ${CMAKE_SOURCE_DIR} ${ANIMATOR_PROJECT})
        execute_process(
                COMMAND ${CMAKE_BINARY_DIR}/animator-build/animator-export
                        -i ${ANIMATOR_PROJECT_RELATIVE}
                        -o demo/animations/gen
                WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                RESULT_VARIABLE ANIMATOR_EXPORT_RESULT)
        if(NOT ANIMATOR_EXPORT_RESULT EQUAL 0)
            message(FATAL_ERROR
                "animator: exporting ${ANIMATOR_PROJECT_RELATIVE} failed (see output "
                "above). The project or demo/chr/config.toml is the problem -- most "
                "often a sprite naming art the CHR layout no longer has -- so this is "
                "not a toolchain fallback case, and it is fatal: the alternative is "
                "compiling against animation data nobody can account for.")
        endif()
    endforeach()
elseif(ANIMATOR_COMMITTED_HEADERS)
    message(STATUS
        "animator: ${ANIMATOR_FALLBACK_REASON}; using the committed "
        "demo/animations/gen/*.hpp instead of re-exporting them from "
        "demo/animations/animations.anim. That is fine for a cross build -- "
        "they are committed -- but edits to the project, to a .msprite, or to "
        "the CHR layout they resolve against will NOT be picked up here. "
        "Re-export on a host with a C++23 compiler, or let CI's animator-check "
        "job catch the drift.")
else()
    message(FATAL_ERROR
        "animator: ${ANIMATOR_FALLBACK_REASON}, and demo/animations/gen/ holds no "
        "generated headers -- nothing to fall back to. Either restore the "
        "committed ones or give this host a C++23 compiler and a toml++ (system "
        "package or network); demo/src reads its animation frames out of those "
        "headers and cannot compile without them.")
endif()

if(_MOS_ENV_CXX)
    set(ENV{CXX} "${_MOS_ENV_CXX}")
else()
    unset(ENV{CXX})
endif()
if(_MOS_ENV_CC)
    set(ENV{CC} "${_MOS_ENV_CC}")
else()
    unset(ENV{CC})
endif()

if(TARGET_PLATFORM STREQUAL "nes")


    if(NOT PNES_AUDIO_SECTION)
        message(FATAL_ERROR
                "PNES_AUDIO_SECTION is not set. It names the ELF section "
                "src/nes/audio/famistudio.cpp is placed into, and must be the bank "
                "the audio engine is assembled into. See local.cmake.example.")
    endif()
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/nes/audio/famistudio.cpp
            PROPERTIES COMPILE_DEFINITIONS
            "PNES_AUDIO_SECTION=\"${PNES_AUDIO_SECTION}\"")

    if(NOT PNES_UI_SECTION)
        message(FATAL_ERROR
                "PNES_UI_SECTION is not set. It names the ELF section "
                "src/all/extras/ui/*.cpp is placed into. See local.cmake.example.")
    endif()
    set(PNES_UI_SOURCES
            ${CMAKE_SOURCE_DIR}/src/all/extras/ui/tileslider.cpp
            ${CMAKE_SOURCE_DIR}/src/all/extras/ui/text.cpp)
    set_source_files_properties(${PNES_UI_SOURCES}
            PROPERTIES COMPILE_DEFINITIONS
            "PNES_UI_SECTION=\"${PNES_UI_SECTION}\"")

    if(NOT TITLE)
        message(FATAL_ERROR
                "TITLE is not set. It names the ELF section uitk-generated "
                "title-scene code/data is placed into. See local.cmake.example.")
    endif()
    if(NOT SYSMEM)
        message(FATAL_ERROR
                "SYSMEM is not set. It names the ELF section uitk-generated "
                "code using the \"SYSMEM\" Linker Prefix is placed into. "
                "See local.cmake.example.")
    endif()

    foreach(_mod video)
        string(TOUPPER ${_mod} _MOD)
        if(PNES_${_MOD}_SECTION AND LLVM_MOS_PLATFORM STREQUAL "nes-nrom")
            message(WARNING
                    "PNES_${_MOD}_SECTION is set, but this is an NROM "
                    "build: one fixed 32 KiB bank, nowhere to move code to. "
                    "MODULE_PLACEMENT is inert here, so the section is ignored "
                    "rather than silently costing you the module's inlining.")
        endif()
        if(PNES_${_MOD}_SECTION)
            set_source_files_properties(${CMAKE_SOURCE_DIR}/src/nes/${_mod}.cpp
                    PROPERTIES COMPILE_DEFINITIONS
                    "PNES_${_MOD}_SECTION=\"${PNES_${_MOD}_SECTION}\"")
            message(STATUS "pnes: ${_mod} module placed in "
                           "${PNES_${_MOD}_SECTION} (inlining disabled for it)")
        endif()
    endforeach()

    find_package(llvm-mos-sdk REQUIRED)
    project(pnes CXX)
    set(PLATFORM_DEFINE TARGET_NES "REGION=\"${REGION}\"")

    if(NOT LLVM_MOS_PLATFORM STREQUAL "nes-nrom")
        list(APPEND PLATFORM_DEFINE NES_MAPPER_BANKSWITCHED)
    endif()

    list(APPEND PLATFORM_DEFINE "MAPPER=4" "SUBMAPPER=0")

    if(DEFINED ALTERNATIVE_NAMETABLE)
        list(APPEND PLATFORM_DEFINE "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
    endif()

    if(SAVE_FILE)
        list(APPEND PLATFORM_DEFINE "SAVE_FILE=${SAVE_FILE}")
    endif()

    list(TRANSFORM PLATFORM_DEFINE PREPEND "-D" OUTPUT_VARIABLE PLATFORM_DEFINE_FLAGS)

    FetchContent_MakeAvailable(intsh tuple)
    _make_consh_available()
    _fixup_fetched_cxx_std_targets()

    set(FAMISTUDIO_DIR ${CMAKE_BINARY_DIR}/famistudio)
    set(FAMISTUDIO_BASE_URL "https://raw.githubusercontent.com/BleuBleu/FamiStudio/master/SoundEngine")

    if(NOT EXISTS ${FAMISTUDIO_DIR}/famistudio_ca65.s)
        file(MAKE_DIRECTORY ${FAMISTUDIO_DIR}/NoteTables)

        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/famistudio_ca65.s ${FAMISTUDIO_DIR}/famistudio_ca65.s)
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/famistudio_ca65.h ${FAMISTUDIO_DIR}/famistudio_ca65.h)

        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/NoteTables/famistudio_note_table_lsb.bin
                ${FAMISTUDIO_DIR}/NoteTables/famistudio_note_table_lsb.bin)
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/NoteTables/famistudio_note_table_msb.bin
                ${FAMISTUDIO_DIR}/NoteTables/famistudio_note_table_msb.bin)
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/NoteTables/famistudio_note_table_pal_lsb.bin
                ${FAMISTUDIO_DIR}/NoteTables/famistudio_note_table_pal_lsb.bin)
        file(DOWNLOAD ${FAMISTUDIO_BASE_URL}/NoteTables/famistudio_note_table_pal_msb.bin
                ${FAMISTUDIO_DIR}/NoteTables/famistudio_note_table_pal_msb.bin)
    endif()

    find_program(CA65 ca65 HINTS "${CC65_DIR}/bin" "C:/cc65/bin" REQUIRED)
    get_filename_component(LLVM_MOS_BIN ${CMAKE_C_COMPILER} DIRECTORY)
    find_program(LLVM_READELF llvm-readelf
        HINTS "${LLVM_MOS_BIN}" NO_DEFAULT_PATH REQUIRED)
    find_program(LLVM_NM llvm-nm
        HINTS "${LLVM_MOS_BIN}" NO_DEFAULT_PATH REQUIRED)

    set(LUA_LOG_BUILDER_SRC ${CMAKE_SOURCE_DIR}/tools/luaLogBuilder)
    set(LUA_LOG_BUILDER_BUILD ${LUA_LOG_BUILDER_SRC}/build)
    set(LUA_LOG_BUILDER_BIN ${LUA_LOG_BUILDER_BUILD}/lua_log_builder)

    add_custom_command(
            OUTPUT ${LUA_LOG_BUILDER_BIN}
            COMMAND ${CMAKE_COMMAND} -S ${LUA_LOG_BUILDER_SRC} -B ${LUA_LOG_BUILDER_BUILD}
                    -DCMAKE_BUILD_TYPE=Release
            COMMAND ${CMAKE_COMMAND} --build ${LUA_LOG_BUILDER_BUILD}
            DEPENDS ${LUA_LOG_BUILDER_SRC}/main.cpp ${LUA_LOG_BUILDER_SRC}/CMakeLists.txt
            COMMENT "Building lua_log_builder (host-native logdata.lua generator)"
            VERBATIM
    )
    add_custom_target(lua_log_builder_tool DEPENDS ${LUA_LOG_BUILDER_BIN})

    file(READ ${CMAKE_SOURCE_DIR}/demo/famistudio_config.s FAMISTUDIO_CONFIG_TEXT)

    macro(famistudio_extract var pattern)
        string(REGEX MATCH "${pattern}" _famistudio_match "${FAMISTUDIO_CONFIG_TEXT}")
        if(NOT _famistudio_match)
            message(FATAL_ERROR "demo/famistudio_config.s: couldn't find ${var} (expected to match: ${pattern})")
        endif()
        set(${var} ${CMAKE_MATCH_1})
    endmacro()

    famistudio_extract(FAMISTUDIO_CFG_DPCM_SUPPORT "FAMISTUDIO_CFG_DPCM_SUPPORT[ \t]*=[ \t]*([0-9]+)")
    famistudio_extract(FAMISTUDIO_CFG_SFX_SUPPORT  "FAMISTUDIO_CFG_SFX_SUPPORT[ \t]*=[ \t]*([0-9]+)")
    famistudio_extract(FAMISTUDIO_USE_VOLUME_TRACK "FAMISTUDIO_USE_VOLUME_TRACK[ \t]*=[ \t]*([0-9]+)")

    set(PNES_SFX_STREAMS 2 CACHE STRING
            "FamiStudio SFX streams: how many effects the NES can play at once \
(1-4). Published to C++ as audio::sfxChannels and to the engine as \
FAMISTUDIO_CFG_SFX_STREAMS, so the two cannot disagree.")
    if(NOT PNES_SFX_STREAMS MATCHES "^[1-4]$")
        message(FATAL_ERROR
                "PNES_SFX_STREAMS must be 1-4 (got \"${PNES_SFX_STREAMS}\"). "
                "FamiStudio allocates FAMISTUDIO_SFX_STRUCT_SIZE bytes of RAM "
                "per stream and supports at most four.")
    endif()
    set(FAMISTUDIO_CFG_SFX_STREAMS ${PNES_SFX_STREAMS})

    set(FAMISTUDIO_CFG_NTSC_SUPPORT 1)
    set(FAMISTUDIO_CFG_PAL_SUPPORT 1)

    file(COPY ${FAMISTUDIO_DIR}/famistudio_ca65.h
            DESTINATION ${CMAKE_BINARY_DIR}/famistudio_include)

    if(EXISTS ${FAMISTUDIO_DIR}/famistudio_llvmmos.h)
        file(COPY ${FAMISTUDIO_DIR}/famistudio_llvmmos.h
                DESTINATION ${CMAKE_BINARY_DIR}/famistudio_include)
    endif()

    string(REPLACE "_" "__" FAMISTUDIO_SEGMENT "${PNES_AUDIO_SECTION}")
    string(REPLACE "." "_p" FAMISTUDIO_SEGMENT "${FAMISTUDIO_SEGMENT}")
    set(FAMISTUDIO_SEGMENT "${FAMISTUDIO_SEGMENT}_fx")

    set(PNES_AUDIO_SEGMENT_INC
            ${CMAKE_BINARY_DIR}/famistudio_include/pnes_audio_segment.inc)
    file(CONFIGURE OUTPUT ${PNES_AUDIO_SEGMENT_INC} CONTENT
"; GENERATED by CMakeLists.txt from PNES_AUDIO_SECTION -- do not edit.
; ${PNES_AUDIO_SECTION} in ca65/XO65 segment spelling.
.define PNES_AUDIO_SEGMENT ${FAMISTUDIO_SEGMENT}
")

    set(FAMISTUDIO_OBJ ${CMAKE_BINARY_DIR}/famistudio.o)
    add_custom_command(
            OUTPUT ${FAMISTUDIO_OBJ}
            COMMAND ${CA65}
            -D PNES_SFX_STREAMS=${PNES_SFX_STREAMS}
            -D PNES_AUDIO_SEGMENT_INC=1
            -I ${CMAKE_SOURCE_DIR}/demo
            -I ${CMAKE_BINARY_DIR}/famistudio_include
            -I ${FAMISTUDIO_DIR}
            -I ${FAMISTUDIO_DIR}/NoteTables
            -o ${FAMISTUDIO_OBJ}
            ${CMAKE_SOURCE_DIR}/demo/famistudio_config.s
            DEPENDS ${CMAKE_SOURCE_DIR}/demo/famistudio_config.s
            ${PNES_AUDIO_SEGMENT_INC}
            COMMENT "Assembling FamiStudio"
    )

    find_program(FAMISTUDIO NAMES famistudio FamiStudio REQUIRED)

    set(NES_BGM_FMS ${CMAKE_SOURCE_DIR}/demo/audio/bgm.fms)
    set(NES_AUDIO_ASM ${CMAKE_SOURCE_DIR}/demo/audio/tracks/nes_audio.s)
    add_custom_command(
            OUTPUT ${NES_AUDIO_ASM}
            COMMAND ${FAMISTUDIO} ${NES_BGM_FMS} famistudio-asm-export ${NES_AUDIO_ASM}
                    -famistudio-asm-format:ca65
            DEPENDS ${NES_BGM_FMS}
            COMMENT "Exporting FamiStudio music: bgm.fms -> nes_audio.s (ca65)"
    )

    set(NES_SFX_FMS ${CMAKE_SOURCE_DIR}/demo/audio/sfx.fms)
    set(NES_SFX_ASM ${CMAKE_BINARY_DIR}/sfx.s)
    set(NES_SFX_MODE dual)
    set(NES_SFX_LIST ${CMAKE_BINARY_DIR}/sfx_sfxlist.inc)
    add_custom_command(
            OUTPUT ${NES_SFX_ASM} ${NES_SFX_LIST}
            COMMAND ${FAMISTUDIO} ${NES_SFX_FMS} famistudio-asm-sfx-export ${NES_SFX_ASM}
                    -famistudio-asm-format:ca65
                    -famistudio-asm-sfx-mode:${NES_SFX_MODE}
                    -famistudio-sfx-generate-list
            DEPENDS ${NES_SFX_FMS}
            COMMENT "Exporting FamiStudio SFX: sfx.fms -> sfx.s + sfx_sfxlist.inc (ca65, ${NES_SFX_MODE})"
    )

    set(NES_SFX_LIST_HPP ${CMAKE_BINARY_DIR}/gen/sfx.hpp)
    add_custom_command(
            OUTPUT ${NES_SFX_LIST_HPP}
            COMMAND ${CMAKE_COMMAND}
                    -DSFX_LIST_INC=${NES_SFX_LIST}
                    -DOUT_HPP=${NES_SFX_LIST_HPP}
                    -P ${CMAKE_SOURCE_DIR}/tools/sfx_list_to_hpp.cmake
            DEPENDS ${NES_SFX_LIST}
                    ${CMAKE_SOURCE_DIR}/tools/sfx_list_to_hpp.cmake
            COMMENT "Generating sfx.hpp from sfx_sfxlist.inc (effect name enum)"
    )

    file(GLOB_RECURSE DEMO_ASM_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/*.s)
    list(REMOVE_ITEM DEMO_ASM_SOURCES ${CMAKE_SOURCE_DIR}/demo/famistudio_config.s)
    list(REMOVE_ITEM DEMO_ASM_SOURCES ${NES_AUDIO_ASM})
    set(DEMO_ASM_OBJECTS "")
    foreach(ASM_SRC ${DEMO_ASM_SOURCES})
        get_filename_component(ASM_NAME ${ASM_SRC} NAME_WE)
        set(ASM_OBJ ${CMAKE_BINARY_DIR}/${ASM_NAME}.o)
        set(ASM_EXTRA_DEPS "")
        if(ASM_SRC STREQUAL "${CMAKE_SOURCE_DIR}/demo/audio/sfx/nes_sfx_bank.s")
            set(ASM_EXTRA_DEPS ${NES_SFX_ASM})
        endif()
        add_custom_command(
                OUTPUT ${ASM_OBJ}
                COMMAND ${CA65}
                -I ${FAMISTUDIO_DIR}
                -I ${FAMISTUDIO_DIR}/NoteTables
                -I ${CMAKE_SOURCE_DIR}/demo/audio/tracks
                -I ${CMAKE_SOURCE_DIR}/demo/audio/sfx
                -I ${CMAKE_BINARY_DIR}
                -o ${ASM_OBJ}
                ${ASM_SRC}
                DEPENDS ${ASM_SRC} ${NES_AUDIO_ASM} ${ASM_EXTRA_DEPS}
                COMMENT "Assembling ${ASM_NAME}"
        )
        list(APPEND DEMO_ASM_OBJECTS ${ASM_OBJ})
    endforeach()

    file(GLOB_RECURSE NES_SOURCES CONFIGURE_DEPENDS src/nes/*.cpp src/all/*.cpp)
    list(REMOVE_ITEM NES_SOURCES ${CMAKE_SOURCE_DIR}/src/nes/mappers/vrc1.cpp)
    add_library(pnes STATIC ${NES_SOURCES})

    add_dependencies(pnes lua_log_builder_tool)

    target_compile_options(pnes PUBLIC -g)

    target_include_directories(pnes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/umbrella
            ${CMAKE_BINARY_DIR}/famistudio_include
    )
    set(FAMISTUDIO_CFG_DEFINES
            FAMISTUDIO_CFG_NTSC_SUPPORT=${FAMISTUDIO_CFG_NTSC_SUPPORT}
            FAMISTUDIO_CFG_PAL_SUPPORT=${FAMISTUDIO_CFG_PAL_SUPPORT}
            FAMISTUDIO_CFG_DPCM_SUPPORT=${FAMISTUDIO_CFG_DPCM_SUPPORT}
            FAMISTUDIO_CFG_SFX_SUPPORT=${FAMISTUDIO_CFG_SFX_SUPPORT}
            FAMISTUDIO_CFG_SFX_STREAMS=${FAMISTUDIO_CFG_SFX_STREAMS}
            FAMISTUDIO_USE_VOLUME_TRACK=${FAMISTUDIO_USE_VOLUME_TRACK})
    list(TRANSFORM FAMISTUDIO_CFG_DEFINES PREPEND "-D"
            OUTPUT_VARIABLE FAMISTUDIO_CFG_FLAGS)

    target_compile_definitions(pnes PUBLIC
            ${PLATFORM_DEFINE}
            ${FAMISTUDIO_CFG_DEFINES}
    )
    target_link_libraries(pnes PUBLIC br0::intsh br0::tuple br0::consh)

    # Threaded through as raw -D flags as well as target_compile_definitions,
    # because the hand-rolled PASS1_ELF invocation below bypasses every CMake
    # target property and so inherits nothing set on `demo`. Without them the
    # generated sources fail that diagnostic build on the very #error guards
    # these defines exist to satisfy.
    set(DEMO_PLACEMENT_DEFINES
            "TITLE=CREATE_SEGMENT_KEYWORD(\"${TITLE}\")"
            "TITLE_DATA=CREATE_SEGMENT_KEYWORD(\"${TITLE}.rodata\")"
            "SYSMEM=CREATE_SEGMENT_KEYWORD(\"${SYSMEM}\")"
            "OPTIONS=CREATE_SEGMENT_KEYWORD(\".prg_rom_options\")"
            "OPTIONS_DATA=CREATE_SEGMENT_KEYWORD(\".prg_rom_options.rodata\")")
    list(TRANSFORM DEMO_PLACEMENT_DEFINES PREPEND "-D" OUTPUT_VARIABLE DEMO_PLACEMENT_FLAGS)

    file(GLOB_RECURSE DEMO_C_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/*.cpp)
    list(FILTER DEMO_C_SOURCES EXCLUDE REGEX "/demo/gen/(title|gameOptions)/")
    file(GLOB DEMO_TITLE_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/gen/title/nes/*.cpp)
    list(APPEND DEMO_C_SOURCES ${DEMO_TITLE_SOURCES})
    file(GLOB DEMO_GAMEOPTIONS_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/demo/gen/gameOptions/nes/*.cpp)
    list(APPEND DEMO_C_SOURCES ${DEMO_GAMEOPTIONS_SOURCES})

    set(PASS1_ELF ${CMAKE_BINARY_DIR}/demo_pass1.elf)
    add_custom_command(
            OUTPUT ${PASS1_ELF}
            COMMAND ${CMAKE_CXX_COMPILER}
            -std=gnu++23 -Wno-c23-extensions -x c++
            -include ${CMAKE_SOURCE_DIR}/demo/src/graphics/charmaps.hpp
            -Os ${PLATFORM_DEFINE_FLAGS} ${DEMO_PLACEMENT_FLAGS} ${FAMISTUDIO_CFG_FLAGS} -DGEN_TARGET_DIR=nes -D${ORIENTATION}
            -mlto-zp=0
            -T ${TOPLEVEL_LD}
            -L${CMAKE_SOURCE_DIR}/src/nes/mappers
            -I${CMAKE_SOURCE_DIR}/include
            -I${CMAKE_SOURCE_DIR}/include/umbrella
            -I${CMAKE_BINARY_DIR}/famistudio_include
            -I${CMAKE_BINARY_DIR}/gen
            -I${intsh_SOURCE_DIR}
            -I${tuple_SOURCE_DIR}
            -I${consh_SOURCE_DIR}
            ${DEMO_C_SOURCES}
            -x none
            $<TARGET_FILE:pnes>
            ${FAMISTUDIO_OBJ}
            ${DEMO_ASM_OBJECTS}
            -o ${CMAKE_BINARY_DIR}/demo_pass1
            DEPENDS pnes
            ${FAMISTUDIO_OBJ}
            ${DEMO_ASM_OBJECTS}
            ${DEMO_C_SOURCES}
            ${NES_SFX_LIST_HPP}
            COMMENT "Linking diagnostic ELF for ZP/BSS measurement"
            VERBATIM
    )


    set(CA65_PADDING ${CMAKE_BINARY_DIR}/ca65_padding.s)
    add_custom_command(
            OUTPUT ${CA65_PADDING}
            COMMAND ${CMAKE_COMMAND}
            -DREADELF=${LLVM_READELF}
            -DELF_FILE=${PASS1_ELF}
            -DOUTPUT_FILE=${CA65_PADDING}
            -P ${CMAKE_SOURCE_DIR}/tools/measure_sections.cmake
            DEPENDS ${PASS1_ELF}
            ${CMAKE_SOURCE_DIR}/tools/measure_sections.cmake
            COMMENT "Extracting ZP/BSS sizes (diagnostic only)"
    )


    set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/chr_rom.cpp
            PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/demo/chr/chr.bin")

    add_executable(demo ${DEMO_C_SOURCES})
    set_target_properties(demo PROPERTIES SUFFIX ".nes")

    target_link_libraries(demo PRIVATE pnes)

    target_include_directories(demo PRIVATE ${CMAKE_BINARY_DIR}/gen)

    target_compile_definitions(demo PRIVATE GEN_TARGET_DIR=nes)

    target_compile_definitions(demo PRIVATE ${DEMO_PLACEMENT_DEFINES})

    target_compile_options(demo PRIVATE -Wno-c23-extensions)

    add_dependencies(demo demo_famistudio_asm)

    add_custom_target(demo_famistudio_asm
            DEPENDS ${FAMISTUDIO_OBJ} ${DEMO_ASM_OBJECTS} ${CA65_PADDING}
                    ${NES_SFX_LIST_HPP}
    )

    add_custom_command(TARGET demo POST_BUILD
            COMMAND ${CMAKE_COMMAND}
            -DREADELF=${LLVM_READELF}
            -DNM=${LLVM_NM}
            -DELF_FILE=$<TARGET_FILE:demo>.elf
            -P ${CMAKE_SOURCE_DIR}/tools/report_space.cmake
            VERBATIM
    )

    add_dependencies(demo lua_log_builder_tool)
    add_custom_command(TARGET demo POST_BUILD
            COMMAND ${LUA_LOG_BUILDER_BIN}
            --elf $<TARGET_FILE:demo>.elf
            --output $<TARGET_FILE_DIR:demo>/logdata.lua
            VERBATIM
    )

    find_program(PNES_PYTHON3 NAMES python3 python)
    if(PNES_PYTHON3)
        add_custom_command(TARGET demo POST_BUILD
                COMMAND ${PNES_PYTHON3} ${CMAKE_SOURCE_DIR}/tools/fix_mlb.py
                $<TARGET_FILE_DIR:demo>/$<TARGET_FILE_BASE_NAME:demo>
                VERBATIM
        )
    else()
        message(STATUS "pnes: python3 not found -- skipping the .mlb fixup, so "
                       "Mesen will show bare PRG addresses instead of symbol "
                       "names (tools/fix_mlb.py can be run by hand later)")
    endif()

    target_link_options(demo PRIVATE
            -mlto-zp=0   # match pass-1 cap; disables mos-zero-page-alloc entirely, see pass-1's comment
            ${FAMISTUDIO_OBJ}
            ${DEMO_ASM_OBJECTS}
    )

elseif(TARGET_PLATFORM STREQUAL "gc" OR TARGET_PLATFORM STREQUAL "gc_ntscj" OR TARGET_PLATFORM STREQUAL "wii")
    project(pnes CXX)

    FetchContent_MakeAvailable(intsh tuple)
    _make_consh_available()
    _fixup_fetched_cxx_std_targets()

    if(TARGET_PLATFORM STREQUAL "gc_ntscj")
        set(REGION "JP")
    endif()

    if(NOT TARGET_PLATFORM STREQUAL "wii")
        set(PLATFORM_DEFINE TARGET_GC)
        set(DEMO_TITLE_DIR gcn)
    else()
        set(PLATFORM_DEFINE TARGET_WII)
        set(DEMO_TITLE_DIR wii)
    endif()

    if(DEFINED ALTERNATIVE_NAMETABLE)
        list(APPEND PLATFORM_DEFINE "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
    endif()

    if(NOT TARGET_PLATFORM STREQUAL "wii")
        set(OGC_GAME_CODE    "PNES"               CACHE STRING "GameCube save gamecode (exactly 4 ASCII)")
        set(OGC_COMPANY_CODE "BR"                 CACHE STRING "GameCube save company code (exactly 2 ASCII)")
        set(OGC_FILENAME     "super_mary_sisters" CACHE STRING "GameCube save file name (at most 31 ASCII)")

        string(LENGTH "${OGC_GAME_CODE}"    _ogc_gamecode_len)
        string(LENGTH "${OGC_COMPANY_CODE}" _ogc_company_len)
        string(LENGTH "${OGC_FILENAME}"     _ogc_filename_len)
        if(NOT _ogc_gamecode_len EQUAL 4)
            message(FATAL_ERROR "OGC_GAME_CODE must be exactly 4 characters, got \"${OGC_GAME_CODE}\" (${_ogc_gamecode_len})")
        endif()
        if(NOT _ogc_company_len EQUAL 2)
            message(FATAL_ERROR "OGC_COMPANY_CODE must be exactly 2 characters, got \"${OGC_COMPANY_CODE}\" (${_ogc_company_len})")
        endif()
        if(_ogc_filename_len EQUAL 0 OR _ogc_filename_len GREATER 31)
            message(FATAL_ERROR
                "OGC_FILENAME must be 1-31 characters, got \"${OGC_FILENAME}\" (${_ogc_filename_len}) "
                "-- CARD_FILENAMELEN is 32 including the terminator")
        endif()

    else()
        set(WII_SAVE_DIR      "super_mary_sisters" CACHE STRING "Wii save directory under the SD/USB volume root")
        set(WII_SAVE_FILENAME "save.dat"           CACHE STRING "Wii save file name inside WII_SAVE_DIR")

        foreach(_wii_knob WII_SAVE_DIR WII_SAVE_FILENAME)
            string(LENGTH "${${_wii_knob}}" _wii_knob_len)
            if(_wii_knob_len EQUAL 0 OR _wii_knob_len GREATER 255)
                message(FATAL_ERROR
                    "${_wii_knob} must be 1-255 characters, got \"${${_wii_knob}}\" (${_wii_knob_len}) "
                    "-- it is one FAT long-name component, not a path")
            endif()
            if("${${_wii_knob}}" MATCHES "[/\\:]")
                message(FATAL_ERROR
                    "${_wii_knob} must be a single name with no '/', '\\' or ':', got \"${${_wii_knob}}\" "
                    "-- src/ogc/save.cpp adds the separators and the volume prefix itself")
            endif()
        endforeach()
    endif()

    file(GLOB OGC_SOURCES CONFIGURE_DEPENDS src/ogc/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
    file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/all/*.cpp)
    add_library(pnes STATIC ${OGC_SOURCES} ${ALL_SOURCES})
    target_include_directories(pnes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/umbrella
    )
    target_compile_definitions(pnes PUBLIC ${PLATFORM_DEFINE} TARGET_OGC)

    if(NOT TARGET_PLATFORM STREQUAL "wii")
        target_compile_definitions(pnes PRIVATE
                "OGC_GAME_CODE=\"${OGC_GAME_CODE}\""
                "OGC_COMPANY_CODE=\"${OGC_COMPANY_CODE}\""
                "OGC_FILENAME=\"${OGC_FILENAME}\"")

    else()
        target_compile_definitions(pnes PRIVATE
                "WII_SAVE_DIR=\"${WII_SAVE_DIR}\""
                "WII_SAVE_FILENAME=\"${WII_SAVE_FILENAME}\"")
    endif()

    target_compile_definitions(pnes PRIVATE $<$<CONFIG:Debug>:OGC_PROFILER>)
    target_link_libraries(pnes PUBLIC br0::intsh br0::tuple br0::consh asnd)

    if(TARGET_PLATFORM STREQUAL "wii")
        target_link_libraries(pnes PUBLIC fat)
    endif()

    find_program(FFMPEG ffmpeg REQUIRED)
    FetchContent_Declare(
            stb
            GIT_REPOSITORY https://github.com/nothings/stb.git
            GIT_TAG        ${STB_GIT_TAG}
    )
    FetchContent_MakeAvailable(stb)
    target_include_directories(pnes PRIVATE ${stb_SOURCE_DIR})

    set(OGC_ASSET_DIR ${CMAKE_BINARY_DIR}/ogc-assets)
    set(OGC_OGG ${OGC_ASSET_DIR}/pc_audio.ogg)
    set(OGC_WAV ${CMAKE_SOURCE_DIR}/demo/audio/tracks/pc_audio.wav)
    file(MAKE_DIRECTORY ${OGC_ASSET_DIR})
    add_custom_command(
            OUTPUT ${OGC_OGG}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${OGC_ASSET_DIR}
            COMMAND ${FFMPEG} -y -loglevel error -i ${OGC_WAV}
                    -ar 44100 -ac 2 -c:a libvorbis -q:a 4 ${OGC_OGG}
            DEPENDS ${OGC_WAV}
            COMMENT "Encoding GC/Wii audio: pc_audio.wav -> pc_audio.ogg (Vorbis, embedded)"
            VERBATIM)
    add_custom_target(ogc-assets DEPENDS ${OGC_OGG})
    add_dependencies(pnes ogc-assets)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/src/ogc/audio.cpp
            PROPERTIES
                OBJECT_DEPENDS  "${OGC_OGG};${OGC_SFX_OUTPUTS};${OGC_SFX_INC}"
                INCLUDE_DIRECTORIES "${OGC_ASSET_DIR}"
                COMPILE_OPTIONS "--embed-dir=${OGC_ASSET_DIR}")

    file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)
    list(FILTER DEMO_SOURCES EXCLUDE REGEX "/demo/gen/(title|gameOptions)/")
    file(GLOB DEMO_TITLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/title/${DEMO_TITLE_DIR}/*.cpp")
    list(APPEND DEMO_SOURCES ${DEMO_TITLE_SOURCES})
    file(GLOB DEMO_GAMEOPTIONS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/gameOptions/${DEMO_TITLE_DIR}/*.cpp")
    list(APPEND DEMO_SOURCES ${DEMO_GAMEOPTIONS_SOURCES})

    set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/chr_rom.cpp
            PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/demo/chr/chr.bin")

    add_executable(demo ${DEMO_SOURCES})
    set_target_properties(demo PROPERTIES SUFFIX ".elf")
    target_link_libraries(demo PRIVATE pnes)

    set(OGC_SFX_DIR ${CMAKE_SOURCE_DIR}/demo/audio/sfx CACHE PATH
            "Directory of the project's one-shot sound effects (*.ogg). Each is \
transcoded to raw PCM and #embedded into the GC/Wii .dol.")
    set(OGC_SFX_OUT_DIR ${OGC_ASSET_DIR}/sfx)
    set(OGC_SFX_INC ${OGC_ASSET_DIR}/sfx_embed.inc)
    set(OGC_SFX_TRIM "areverse,silenceremove=start_periods=1:start_threshold=-60dB:start_silence=0.05:detection=peak,areverse")
    file(GLOB OGC_SFX_SOURCES CONFIGURE_DEPENDS ${OGC_SFX_DIR}/*.ogg)
    set(OGC_SFX_OUTPUTS)
    set(OGC_SFX_INC_BODY "// GENERATED by the gc/wii branch of CMakeLists.txt -- do not edit.\n")
    string(APPEND OGC_SFX_INC_BODY "// One #embed per effect, plus the name table audio.cpp matches ::sfxs against.\n\n")
    set(OGC_SFX_INC_TABLE "")
    set(OGC_SFX_INDEX 0)
    foreach(OGC_SFX_SRC ${OGC_SFX_SOURCES})
        get_filename_component(OGC_SFX_NAME ${OGC_SFX_SRC} NAME_WE)
        set(OGC_SFX_RAW ${OGC_SFX_OUT_DIR}/${OGC_SFX_NAME}.raw)
        add_custom_command(
                OUTPUT ${OGC_SFX_RAW}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${OGC_SFX_OUT_DIR}
                COMMAND ${FFMPEG} -y -loglevel error -i ${OGC_SFX_SRC}
                        -af ${OGC_SFX_TRIM}
                        -ar 48000 -ac 2 -f s16be ${OGC_SFX_RAW}
                DEPENDS ${OGC_SFX_SRC}
                COMMENT "Encoding GC/Wii SFX: ${OGC_SFX_NAME}.ogg -> embedded sfx/${OGC_SFX_NAME}.raw (48k s16be)"
                VERBATIM)
        list(APPEND OGC_SFX_OUTPUTS ${OGC_SFX_RAW})
        string(APPEND OGC_SFX_INC_BODY
                "alignas(32) static const unsigned char sfx_blob_${OGC_SFX_INDEX}[] = {\n"
                "#embed <sfx/${OGC_SFX_NAME}.raw>\n"
                "};\n")
        string(APPEND OGC_SFX_INC_TABLE
                "    { \"${OGC_SFX_NAME}\", sfx_blob_${OGC_SFX_INDEX}, sizeof(sfx_blob_${OGC_SFX_INDEX}) },\n")
        math(EXPR OGC_SFX_INDEX "${OGC_SFX_INDEX} + 1")
    endforeach()
    string(APPEND OGC_SFX_INC_BODY "\nstatic const sfx_embed_t sfx_embeds[] = {\n${OGC_SFX_INC_TABLE}};\n")
    file(CONFIGURE OUTPUT ${OGC_SFX_INC} CONTENT "${OGC_SFX_INC_BODY}" @ONLY)

    add_custom_target(ogc-sfx-assets DEPENDS ${OGC_SFX_OUTPUTS})
    add_dependencies(pnes ogc-sfx-assets)

    # Memory-card banner and icon, GameCube only -- the Wii in this branch has
    # no card to put one on.
    #
    # What the system menu reads is a GX texture rather than a PNG: CI8 pixels
    # in 8x4 tiles followed by a 256-entry big-endian RGB5A3 palette, which is
    # what tools/ogc_card_image.py exists for. The sizes are fixed by hardware
    # and match what save.hpp declares, so a mismatch is a compile error at the
    # #embed rather than a corrupt save.
    #
    # One icon frame, deliberately -- but frame 0's speed must still be nonzero,
    # or libogc forces its format to 0 and a static icon renders as nothing.
    if(NOT TARGET_PLATFORM STREQUAL "wii")
        set(OGC_CARD_ART_DIR ${CMAKE_SOURCE_DIR}/demo/sys_assets/ogc CACHE PATH
                "Directory holding the GameCube memory-card banner.png (96x32) and \
icon.png (32x32). Both are converted to CI8 and embedded in the save file.")
        set(OGC_CARD_OUT_DIR ${OGC_ASSET_DIR}/card)
        set(OGC_CARD_PACKER ${CMAKE_SOURCE_DIR}/tools/ogc_card_image.py)

        find_program(IMAGEMAGICK_CONVERT NAMES magick convert)
        find_program(PNES_PYTHON3 NAMES python3 python)
        if(NOT IMAGEMAGICK_CONVERT)
            message(FATAL_ERROR
                    "pnes: ImageMagick (magick/convert) not found -- it decodes the "
                    "memory-card banner/icon PNGs in ${OGC_CARD_ART_DIR} that "
                    "demo/src/modes/pregame.cpp embeds. Install it (docker/"
                    "Dockerfile.ogc does) or point OGC_CARD_ART_DIR at prebuilt art.")
        endif()
        if(NOT PNES_PYTHON3)
            message(FATAL_ERROR
                    "pnes: python3 not found -- tools/ogc_card_image.py packs the "
                    "memory-card banner/icon into the CI8 form the GameCube system "
                    "menu reads, and nothing else can produce it. Install python3 "
                    "(docker/Dockerfile.ogc does).")
        endif()

        set(OGC_CARD_OUTPUTS)
        foreach(_card_art banner icon)
            if(_card_art STREQUAL "banner")
                set(_card_w 96)
                set(_card_h 32)
            else()
                set(_card_w 32)
                set(_card_h 32)
            endif()
            set(_card_src ${OGC_CARD_ART_DIR}/${_card_art}.png)
            set(_card_raw ${OGC_CARD_OUT_DIR}/${_card_art}.rgba)
            set(_card_ci  ${OGC_CARD_OUT_DIR}/${_card_art}.ci)
            add_custom_command(
                    OUTPUT ${_card_ci}
                    BYPRODUCTS ${_card_raw}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${OGC_CARD_OUT_DIR}
                    COMMAND ${IMAGEMAGICK_CONVERT} ${_card_src}
                            -alpha on -resize ${_card_w}x${_card_h}!
                            -colors 256 -depth 8 RGBA:${_card_raw}
                    COMMAND ${PNES_PYTHON3} ${OGC_CARD_PACKER}
                            --rgba ${_card_raw}
                            --width ${_card_w} --height ${_card_h}
                            --out ${_card_ci} --label ${_card_art}
                    DEPENDS ${_card_src} ${OGC_CARD_PACKER}
                    COMMENT "Packing GC card ${_card_art}: ${_card_art}.png -> embedded card/${_card_art}.ci (CI8 + RGB5A3)"
                    VERBATIM)
            list(APPEND OGC_CARD_OUTPUTS ${_card_ci})
        endforeach()

        add_custom_target(ogc-card-assets DEPENDS ${OGC_CARD_OUTPUTS})
        add_dependencies(demo ogc-card-assets)

        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/save.cpp
                PROPERTIES
                    OBJECT_DEPENDS  "${OGC_CARD_OUTPUTS}"
                    INCLUDE_DIRECTORIES "${OGC_ASSET_DIR}"
                    COMPILE_OPTIONS "--embed-dir=${OGC_ASSET_DIR}")
    endif()

    target_compile_definitions(demo PRIVATE "GEN_TARGET_DIR=${DEMO_TITLE_DIR}")

    target_compile_options(demo PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
    if(_ipo_ok)
        foreach(_cfg Release MinSizeRel RelWithDebInfo)
            string(TOUPPER ${_cfg} _CFG)
            set_property(TARGET demo pnes
                    PROPERTY INTERPROCEDURAL_OPTIMIZATION_${_CFG} ON)
        endforeach()
    else()
        message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
    endif()

    ogc_create_dol(demo)

elseif(TARGET_PLATFORM STREQUAL "ctr")
    project(pnes CXX)

    FetchContent_MakeAvailable(intsh tuple)
    _make_consh_available()
    _fixup_fetched_cxx_std_targets()

    set(DEMO_TITLE_DIR 3ds)

    file(GLOB CTR_SOURCES CONFIGURE_DEPENDS src/3ds/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
    file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/all/*.cpp)
    add_library(pnes STATIC ${CTR_SOURCES} ${ALL_SOURCES})
    target_include_directories(pnes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/umbrella
    )
    target_compile_definitions(pnes PUBLIC TARGET_CTR)
    if(DEFINED ALTERNATIVE_NAMETABLE)
        target_compile_definitions(pnes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
    endif()
    target_link_libraries(pnes PUBLIC br0::intsh br0::tuple br0::consh)

    set(CTR_SAVE_DIR      "super_mary_sisters" CACHE STRING "3DS save directory under sdmc:/3ds")
    set(CTR_SAVE_FILENAME "save.dat"           CACHE STRING "3DS save file name inside CTR_SAVE_DIR")

    foreach(_ctr_knob CTR_SAVE_DIR CTR_SAVE_FILENAME)
        string(LENGTH "${${_ctr_knob}}" _ctr_knob_len)
        if(_ctr_knob_len EQUAL 0 OR _ctr_knob_len GREATER 255)
            message(FATAL_ERROR
                "${_ctr_knob} must be 1-255 characters, got \"${${_ctr_knob}}\" (${_ctr_knob_len}) "
                "-- it is one FAT long-name component, not a path")
        endif()
        if("${${_ctr_knob}}" MATCHES "[/\\:]")
            message(FATAL_ERROR
                "${_ctr_knob} must be a single name with no '/', '\\' or ':', got \"${${_ctr_knob}}\" "
                "-- src/3ds/save.cpp adds the separators and the sdmc:/3ds prefix itself")
        endif()
    endforeach()

    target_compile_definitions(pnes PRIVATE
            "CTR_SAVE_DIR=\"${CTR_SAVE_DIR}\""
            "CTR_SAVE_FILENAME=\"${CTR_SAVE_FILENAME}\"")


    find_program(FFMPEG ffmpeg REQUIRED)
    set(CTR_AUDIO_RATE 32728)
    set(CTR_ROMFS ${CMAKE_BINARY_DIR}/romfs)

    FetchContent_Declare(
            stb
            GIT_REPOSITORY https://github.com/nothings/stb.git
            GIT_TAG        ${STB_GIT_TAG}
    )
    FetchContent_MakeAvailable(stb)
    target_include_directories(pnes PRIVATE ${stb_SOURCE_DIR})

    file(MAKE_DIRECTORY ${CTR_ROMFS}/tracks)
    file(MAKE_DIRECTORY ${CTR_ROMFS}/sfx)

    set(CTR_OGG ${CTR_ROMFS}/tracks/pc_audio.ogg)
    set(CTR_WAV ${CMAKE_SOURCE_DIR}/demo/audio/tracks/pc_audio.wav)
    add_custom_command(
            OUTPUT ${CTR_OGG}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CTR_ROMFS}/tracks
            COMMAND ${FFMPEG} -y -loglevel error -i ${CTR_WAV}
                    -ar ${CTR_AUDIO_RATE} -ac 2 -c:a libvorbis -q:a 4 ${CTR_OGG}
            DEPENDS ${CTR_WAV}
            COMMENT "Encoding 3DS audio: pc_audio.wav -> RomFS pc_audio.ogg (Vorbis ${CTR_AUDIO_RATE}Hz)"
            VERBATIM)

    set(CTR_SFX_DIR ${CMAKE_SOURCE_DIR}/demo/audio/sfx CACHE PATH
            "Directory of the project's one-shot sound effects (*.ogg), \
transcoded into the .3dsx's RomFS as sfx/<name>.raw.")
    set(CTR_SFX_TRIM "areverse,silenceremove=start_periods=1:start_threshold=-60dB:start_silence=0.05:detection=peak,areverse")
    file(GLOB CTR_SFX_SOURCES CONFIGURE_DEPENDS ${CTR_SFX_DIR}/*.ogg)
    set(CTR_SFX_OUTPUTS)
    foreach(CTR_SFX_SRC ${CTR_SFX_SOURCES})
        get_filename_component(CTR_SFX_NAME ${CTR_SFX_SRC} NAME_WE)
        set(CTR_SFX_RAW ${CTR_ROMFS}/sfx/${CTR_SFX_NAME}.raw)
        add_custom_command(
                OUTPUT ${CTR_SFX_RAW}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${CTR_ROMFS}/sfx
                COMMAND ${FFMPEG} -y -loglevel error -i ${CTR_SFX_SRC}
                        -af ${CTR_SFX_TRIM}
                        -ar ${CTR_AUDIO_RATE} -ac 2 -f s16le ${CTR_SFX_RAW}
                DEPENDS ${CTR_SFX_SRC}
                COMMENT "Encoding 3DS SFX: ${CTR_SFX_NAME}.ogg -> RomFS sfx/${CTR_SFX_NAME}.raw (${CTR_AUDIO_RATE} s16le)"
                VERBATIM)
        list(APPEND CTR_SFX_OUTPUTS ${CTR_SFX_RAW})
    endforeach()

    add_custom_target(ctr-assets DEPENDS ${CTR_OGG} ${CTR_SFX_OUTPUTS})

    file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)
    list(FILTER DEMO_SOURCES EXCLUDE REGEX "/demo/gen/(title|gameOptions)/")
    file(GLOB DEMO_TITLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/title/${DEMO_TITLE_DIR}/*.cpp")
    list(APPEND DEMO_SOURCES ${DEMO_TITLE_SOURCES})
    file(GLOB DEMO_GAMEOPTIONS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/gameOptions/${DEMO_TITLE_DIR}/*.cpp")
    list(APPEND DEMO_SOURCES ${DEMO_GAMEOPTIONS_SOURCES})

    set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/chr_rom.cpp
            PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/demo/chr/chr.bin")

    add_executable(demo_elf ${DEMO_SOURCES})
    set_target_properties(demo_elf PROPERTIES OUTPUT_NAME demo SUFFIX ".elf")
    target_link_libraries(demo_elf PRIVATE pnes citro2d citro3d ctru m)

    add_dependencies(demo_elf ctr-assets)

    target_compile_definitions(demo_elf PRIVATE "GEN_TARGET_DIR=${DEMO_TITLE_DIR}")

    target_compile_options(demo_elf PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
    if(_ipo_ok)
        foreach(_cfg Release MinSizeRel RelWithDebInfo)
            string(TOUPPER ${_cfg} _CFG)
            set_property(TARGET demo_elf pnes
                    PROPERTY INTERPROCEDURAL_OPTIMIZATION_${_CFG} ON)
        endforeach()
    else()
        message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
    endif()

    ctr_generate_smdh(demo.smdh
            NAME "pnes demo"
            DESCRIPTION "NES engine demo on 3DS (citro2d)"
            AUTHOR "brette"
    )
    ctr_create_3dsx(demo
            TARGET demo_elf
            SMDH ${CMAKE_BINARY_DIR}/demo.smdh
            ROMFS ${CTR_ROMFS}
    )

    elseif(TARGET_PLATFORM STREQUAL "switch")
        project(pnes CXX)

        FetchContent_MakeAvailable(intsh tuple)
        _make_consh_available()
        _fixup_fetched_cxx_std_targets()

        set(DEMO_TITLE_DIR switch)

        file(GLOB NX_SOURCES CONFIGURE_DEPENDS src/switch/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
        file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/all/*.cpp)
        add_library(pnes STATIC ${NX_SOURCES} ${ALL_SOURCES})
        target_include_directories(pnes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
        )
        target_compile_definitions(pnes PUBLIC TARGET_NX)
        if(DEFINED ALTERNATIVE_NAMETABLE)
            target_compile_definitions(pnes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
        endif()
        target_link_libraries(pnes PUBLIC br0::intsh br0::tuple br0::consh)

        set(NX_SAVE_DIR      "super_mary_sisters" CACHE STRING "Switch save directory under sdmc:/switch")
        set(NX_SAVE_FILENAME "save.dat"           CACHE STRING "Switch save file name inside NX_SAVE_DIR")

        foreach(_nx_knob NX_SAVE_DIR NX_SAVE_FILENAME)
            string(LENGTH "${${_nx_knob}}" _nx_knob_len)
            if(_nx_knob_len EQUAL 0 OR _nx_knob_len GREATER 255)
                message(FATAL_ERROR
                    "${_nx_knob} must be 1-255 characters, got \"${${_nx_knob}}\" (${_nx_knob_len}) "
                    "-- it is one FAT long-name component, not a path")
            endif()
            if("${${_nx_knob}}" MATCHES "[/\\:]")
                message(FATAL_ERROR
                    "${_nx_knob} must be a single name with no '/', '\\' or ':', got \"${${_nx_knob}}\" "
                    "-- src/switch/save.cpp adds the separators and the sdmc:/switch prefix itself")
            endif()
        endforeach()

        target_compile_definitions(pnes PRIVATE
                "NX_SAVE_DIR=\"${NX_SAVE_DIR}\""
                "NX_SAVE_FILENAME=\"${NX_SAVE_FILENAME}\"")


        file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)
        list(FILTER DEMO_SOURCES EXCLUDE REGEX "/demo/gen/(title|gameOptions)/")
        file(GLOB DEMO_TITLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/title/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_TITLE_SOURCES})
        file(GLOB DEMO_GAMEOPTIONS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/gameOptions/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_GAMEOPTIONS_SOURCES})

        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/chr_rom.cpp
                PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/demo/chr/chr.bin")

        add_executable(demo_elf ${DEMO_SOURCES})
        set_target_properties(demo_elf PROPERTIES OUTPUT_NAME demo SUFFIX ".elf")
        target_link_libraries(demo_elf PRIVATE pnes nx)

        target_compile_definitions(demo_elf PRIVATE "GEN_TARGET_DIR=${DEMO_TITLE_DIR}")

        target_compile_options(demo_elf PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

        include(CheckIPOSupported)
        check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
        if(_ipo_ok)
            foreach(_cfg Release MinSizeRel RelWithDebInfo)
                string(TOUPPER ${_cfg} _CFG)
                set_property(TARGET demo_elf pnes
                        PROPERTY INTERPROCEDURAL_OPTIMIZATION_${_CFG} ON)
            endforeach()
        else()
            message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
        endif()

        find_program(FFMPEG ffmpeg REQUIRED)
        set(NX_ROMFS ${CMAKE_BINARY_DIR}/romfs)
        set(NX_RAW   ${NX_ROMFS}/tracks/pc_audio.raw)
        set(NX_WAV   ${CMAKE_SOURCE_DIR}/demo/audio/tracks/pc_audio.wav)
        file(MAKE_DIRECTORY ${NX_ROMFS}/tracks)
        add_custom_command(
                OUTPUT ${NX_RAW}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${NX_ROMFS}/tracks
                COMMAND ${FFMPEG} -y -loglevel error -i ${NX_WAV}
                        -ar 48000 -ac 2 -f s16le ${NX_RAW}
                DEPENDS ${NX_WAV}
                COMMENT "Encoding Switch audio: pc_audio.wav -> RomFS pc_audio.raw (48k s16le)"
                VERBATIM)
        set(NX_SFX_DIR ${CMAKE_SOURCE_DIR}/demo/audio/sfx CACHE PATH
                "Directory of the project's one-shot sound effects (*.ogg), \
transcoded into the .nro's RomFS as sfx/<name>.raw.")
        set(NX_SFX_TRIM "areverse,silenceremove=start_periods=1:start_threshold=-60dB:start_silence=0.05:detection=peak,areverse")
        file(MAKE_DIRECTORY ${NX_ROMFS}/sfx)
        file(GLOB NX_SFX_SOURCES CONFIGURE_DEPENDS ${NX_SFX_DIR}/*.ogg)
        set(NX_SFX_OUTPUTS)
        foreach(NX_SFX_SRC ${NX_SFX_SOURCES})
            get_filename_component(NX_SFX_NAME ${NX_SFX_SRC} NAME_WE)
            set(NX_SFX_RAW ${NX_ROMFS}/sfx/${NX_SFX_NAME}.raw)
            add_custom_command(
                    OUTPUT ${NX_SFX_RAW}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${NX_ROMFS}/sfx
                    COMMAND ${FFMPEG} -y -loglevel error -i ${NX_SFX_SRC}
                            -af ${NX_SFX_TRIM}
                            -ar 48000 -ac 2 -f s16le ${NX_SFX_RAW}
                    DEPENDS ${NX_SFX_SRC}
                    COMMENT "Encoding Switch SFX: ${NX_SFX_NAME}.ogg -> RomFS sfx/${NX_SFX_NAME}.raw (48k s16le)"
                    VERBATIM)
            list(APPEND NX_SFX_OUTPUTS ${NX_SFX_RAW})
        endforeach()

        add_custom_target(switch-assets DEPENDS ${NX_RAW} ${NX_SFX_OUTPUTS})
        add_dependencies(demo_elf switch-assets)

        nx_generate_nacp(demo.nacp
                NAME "pnes demo"
                AUTHOR "brette"
                VERSION "1.0.0"
        )
        nx_create_nro(demo
                TARGET demo_elf
                NACP ${CMAKE_BINARY_DIR}/demo.nacp
                ROMFS ${NX_ROMFS}
        )

    elseif(TARGET_PLATFORM STREQUAL "wiiu")
        project(pnes CXX)

        FetchContent_MakeAvailable(intsh tuple)
        _make_consh_available()
        _fixup_fetched_cxx_std_targets()

        set(DEMO_TITLE_DIR "wii u")

        find_package(SDL2 REQUIRED)

        file(GLOB WIIU_SOURCES CONFIGURE_DEPENDS src/wiiu/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
        file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/all/*.cpp)
        add_library(pnes STATIC ${WIIU_SOURCES} ${ALL_SOURCES})
        target_include_directories(pnes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
        )
        target_compile_definitions(pnes PUBLIC TARGET_WIIU)
        if(DEFINED ALTERNATIVE_NAMETABLE)
            target_compile_definitions(pnes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
        endif()
        target_link_libraries(pnes PUBLIC SDL2::SDL2-static br0::intsh br0::tuple br0::consh)

        set(WIIU_SAVE_DIR      "super_mary_sisters" CACHE STRING "Wii U save directory under sd:/wiiu")
        set(WIIU_SAVE_FILENAME "save.dat"           CACHE STRING "Wii U save file name inside WIIU_SAVE_DIR")

        foreach(_wiiu_knob WIIU_SAVE_DIR WIIU_SAVE_FILENAME)
            string(LENGTH "${${_wiiu_knob}}" _wiiu_knob_len)
            if(_wiiu_knob_len EQUAL 0 OR _wiiu_knob_len GREATER 255)
                message(FATAL_ERROR
                    "${_wiiu_knob} must be 1-255 characters, got \"${${_wiiu_knob}}\" (${_wiiu_knob_len}) "
                    "-- it is one FAT long-name component, not a path")
            endif()
            if("${${_wiiu_knob}}" MATCHES "[/\\:]")
                message(FATAL_ERROR
                    "${_wiiu_knob} must be a single name with no '/', '\\' or ':', got \"${${_wiiu_knob}}\" "
                    "-- src/wiiu/save.cpp adds the separators and the fs:/vol/external01/wiiu prefix itself")
            endif()
        endforeach()

        target_compile_definitions(pnes PRIVATE
                "WIIU_SAVE_DIR=\"${WIIU_SAVE_DIR}\""
                "WIIU_SAVE_FILENAME=\"${WIIU_SAVE_FILENAME}\"")

        find_program(FFMPEG ffmpeg REQUIRED)
        FetchContent_Declare(
                stb
                GIT_REPOSITORY https://github.com/nothings/stb.git
                GIT_TAG        ${STB_GIT_TAG}
        )
        FetchContent_MakeAvailable(stb)
        target_include_directories(pnes PRIVATE ${stb_SOURCE_DIR})

        set(WIIU_ASSET_DIR ${CMAKE_BINARY_DIR}/wiiu-assets)
        set(WIIU_OGG ${WIIU_ASSET_DIR}/pc_audio.ogg)
        set(WIIU_WAV ${CMAKE_SOURCE_DIR}/demo/audio/tracks/pc_audio.wav)
        file(MAKE_DIRECTORY ${WIIU_ASSET_DIR})
        add_custom_command(
                OUTPUT ${WIIU_OGG}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${WIIU_ASSET_DIR}
                COMMAND ${FFMPEG} -y -loglevel error -i ${WIIU_WAV}
                        -ar 48000 -ac 2 -c:a libvorbis -q:a 4 ${WIIU_OGG}
                DEPENDS ${WIIU_WAV}
                COMMENT "Encoding Wii U audio: pc_audio.wav -> pc_audio.ogg (Vorbis, embedded)"
                VERBATIM)
        add_custom_target(wiiu-assets DEPENDS ${WIIU_OGG})
        add_dependencies(pnes wiiu-assets)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/src/wiiu/audio.cpp
                PROPERTIES
                    OBJECT_DEPENDS  ${WIIU_OGG}
                    COMPILE_OPTIONS "--embed-dir=${WIIU_ASSET_DIR}")

        file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)
        list(FILTER DEMO_SOURCES EXCLUDE REGEX "/demo/gen/(title|gameOptions)/")
        file(GLOB DEMO_TITLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/title/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_TITLE_SOURCES})
        file(GLOB DEMO_GAMEOPTIONS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/gameOptions/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_GAMEOPTIONS_SOURCES})

        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/chr_rom.cpp
                PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/demo/chr/chr.bin")

        add_executable(demo_elf ${DEMO_SOURCES})
        set_target_properties(demo_elf PROPERTIES OUTPUT_NAME demo SUFFIX ".elf")
        target_link_libraries(demo_elf PRIVATE pnes)

        target_compile_definitions(demo_elf PRIVATE "GEN_TARGET_DIR=${DEMO_TITLE_DIR}")

        target_compile_options(demo_elf PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

        include(CheckIPOSupported)
        check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
        if(_ipo_ok)
            foreach(_cfg Release MinSizeRel RelWithDebInfo)
                string(TOUPPER ${_cfg} _CFG)
                set_property(TARGET demo_elf pnes
                        PROPERTY INTERPROCEDURAL_OPTIMIZATION_${_CFG} ON)
            endforeach()
        else()
            message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
        endif()

        set(WIIU_CONTENT_DIR ${CMAKE_BINARY_DIR}/wuhb-content)
        set(WIIU_SFX_DIR ${CMAKE_SOURCE_DIR}/demo/audio/sfx CACHE PATH
                "Directory of the project's one-shot sound effects (*.ogg), \
copied into the .wuhb content tree (mounted at /vol/content).")
        file(MAKE_DIRECTORY ${WIIU_CONTENT_DIR}/sfx)
        file(GLOB WIIU_SFX_SOURCES CONFIGURE_DEPENDS ${WIIU_SFX_DIR}/*.ogg)
        set(WIIU_SFX_COPY "")
        if(WIIU_SFX_SOURCES)
            set(WIIU_SFX_COPY COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${WIIU_SFX_SOURCES} ${WIIU_CONTENT_DIR}/sfx)
        endif()
        add_custom_target(wiiu-sfx-assets
                COMMAND ${CMAKE_COMMAND} -E make_directory ${WIIU_CONTENT_DIR}/sfx
                ${WIIU_SFX_COPY}
                COMMENT "Staging Wii U SFX: demo/audio/sfx/*.ogg -> /vol/content/sfx"
                VERBATIM)
        add_dependencies(demo_elf wiiu-sfx-assets)

        wut_create_rpx(demo_elf)
        wut_create_wuhb(demo
                TARGET    demo_elf
                CONTENT   ${WIIU_CONTENT_DIR}
                NAME      "pnes demo"
                SHORTNAME "pnes"
                AUTHOR    "brette"
        )

        set(WIIU_TITLE_ID     "000500001337C0DE" CACHE STRING "Wii U title id (16 hex)")
        set(WIIU_GROUP_ID     "0000C0DE"         CACHE STRING "Wii U group id (8 hex)")
        set(WIIU_PRODUCT_CODE "WUP-N-PNES"       CACHE STRING "Wii U product code")
        set(WIIU_COMPANY_CODE "0001"             CACHE STRING "Wii U company code")
        set(WIIU_LONGNAME     "${GAME_NAME}"     CACHE STRING "Wii U menu long name")
        set(WIIU_SHORTNAME    "${GAME_NAME}"     CACHE STRING "Wii U menu short name")
        set(WIIU_PUBLISHER    "brette"           CACHE STRING "Wii U menu publisher")
        set(WIIU_RPX_NAME     "demo.rpx"         CACHE STRING "rpx filename inside the WUP")
        set(WIIU_NUSPACKER_JAR "$ENV{WIIU_NUSPACKER_JAR}" CACHE STRING "Path to NUSPacker.jar")
        set(WIIU_COMMON_KEY   "$ENV{WIIU_COMMON_KEY}"     CACHE STRING "Wii U common key (32 hex) or file path")

        set(WUP_DIR     ${CMAKE_BINARY_DIR}/wup)
        set(WUP_CODE    ${WUP_DIR}/code)
        set(WUP_CONTENT ${WUP_DIR}/content)
        set(WUP_META    ${WUP_DIR}/meta)
        set(WUP_OUT     ${CMAKE_BINARY_DIR}/wup-installable)

        configure_file(${CMAKE_SOURCE_DIR}/src/wiiu/title/app.xml.in  ${WUP_CODE}/app.xml  @ONLY)
        configure_file(${CMAKE_SOURCE_DIR}/src/wiiu/title/cos.xml.in  ${WUP_CODE}/cos.xml  @ONLY)
        configure_file(${CMAKE_SOURCE_DIR}/src/wiiu/title/meta.xml.in ${WUP_META}/meta.xml @ONLY)

        find_program(IMAGEMAGICK_CONVERT NAMES magick convert)
        set(WUP_ICON  ${WUP_META}/iconTex.tga)
        set(WUP_BOOTTV  ${WUP_META}/bootTvTex.tga)
        set(WUP_BOOTDRC ${WUP_META}/bootDrcTex.tga)
        set(WUP_BOOTLOGO ${WUP_META}/bootLogoTex.tga)
        set(WIIU_ICON_SRC ${CMAKE_SOURCE_DIR}/demo/demo.png)
        if(IMAGEMAGICK_CONVERT)
            set(_tga_opts -define tga:image-origin=TopLeft -compress none -type TrueColor)
            add_custom_command(OUTPUT ${WUP_ICON}
                    COMMAND ${IMAGEMAGICK_CONVERT} ${WIIU_ICON_SRC} -resize 128x128! ${_tga_opts} ${WUP_ICON}
                    DEPENDS ${WIIU_ICON_SRC} VERBATIM)
            add_custom_command(OUTPUT ${WUP_BOOTTV}
                    COMMAND ${IMAGEMAGICK_CONVERT} ${WIIU_ICON_SRC} -resize 1280x720 -background black -gravity center -extent 1280x720 ${_tga_opts} ${WUP_BOOTTV}
                    DEPENDS ${WIIU_ICON_SRC} VERBATIM)
            add_custom_command(OUTPUT ${WUP_BOOTDRC}
                    COMMAND ${IMAGEMAGICK_CONVERT} ${WIIU_ICON_SRC} -resize 854x480 -background black -gravity center -extent 854x480 ${_tga_opts} ${WUP_BOOTDRC}
                    DEPENDS ${WIIU_ICON_SRC} VERBATIM)
            add_custom_command(OUTPUT ${WUP_BOOTLOGO}
                    COMMAND ${IMAGEMAGICK_CONVERT} ${WIIU_ICON_SRC} -resize 170x42 -background black -gravity center -extent 170x42 ${_tga_opts} ${WUP_BOOTLOGO}
                    DEPENDS ${WIIU_ICON_SRC} VERBATIM)
            set(WUP_TGAS ${WUP_ICON} ${WUP_BOOTTV} ${WUP_BOOTDRC} ${WUP_BOOTLOGO})
        else()
            message(STATUS "wiiu: ImageMagick not found; demo-wup will skip menu-art (TGA) generation")
            set(WUP_TGAS "")
        endif()

        add_custom_target(demo-wup
                COMMAND ${CMAKE_COMMAND} -E copy
                        ${CMAKE_CURRENT_BINARY_DIR}/${WIIU_RPX_NAME} ${WUP_CODE}/${WIIU_RPX_NAME}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${WUP_CONTENT}
                COMMAND ${CMAKE_COMMAND} -E touch ${WUP_CONTENT}/.keep
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                        ${WIIU_CONTENT_DIR} ${WUP_CONTENT}
                COMMAND ${CMAKE_COMMAND}
                        -DWUP_DIR=${WUP_DIR}
                        -DWUP_OUT=${WUP_OUT}
                        -DNUSPACKER_JAR=${WIIU_NUSPACKER_JAR}
                        -DCOMMON_KEY=${WIIU_COMMON_KEY}
                        -DTITLE_ID=${WIIU_TITLE_ID}
                        -P ${CMAKE_SOURCE_DIR}/cmake/wiiu_pack.cmake
                DEPENDS demo ${WUP_TGAS}
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
                COMMENT "Assembling Wii U installable title (code/content/meta) and packing WUP"
                VERBATIM)

    elseif(TARGET_PLATFORM STREQUAL "psp")
        project(pnes CXX)

        FetchContent_MakeAvailable(intsh tuple)
        _make_consh_available()
        _fixup_fetched_cxx_std_targets()

        set(DEMO_TITLE_DIR psp)

        file(GLOB PSP_SOURCES CONFIGURE_DEPENDS src/psp/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
        file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/all/*.cpp)
        add_library(pnes STATIC ${PSP_SOURCES} ${ALL_SOURCES})
        target_include_directories(pnes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
        )
        target_compile_definitions(pnes PUBLIC TARGET_PSP)
        if(DEFINED ALTERNATIVE_NAMETABLE)
            target_compile_definitions(pnes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
        endif()
        target_link_libraries(pnes PUBLIC br0::intsh br0::tuple br0::consh)

        set(PSP_SAVE_DIR      "super_mary_sisters" CACHE STRING "PSP fallback save directory under ms0:/PSP/GAME")
        set(PSP_SAVE_FILENAME "save.dat"           CACHE STRING "PSP save file name, beside the running EBOOT")

        foreach(_psp_knob PSP_SAVE_DIR PSP_SAVE_FILENAME)
            string(LENGTH "${${_psp_knob}}" _psp_knob_len)
            if(_psp_knob_len EQUAL 0 OR _psp_knob_len GREATER 255)
                message(FATAL_ERROR
                    "${_psp_knob} must be 1-255 characters, got \"${${_psp_knob}}\" (${_psp_knob_len}) "
                    "-- it is one FAT long-name component, not a path")
            endif()
            if("${${_psp_knob}}" MATCHES "[/\\:]")
                message(FATAL_ERROR
                    "${_psp_knob} must be a single name with no '/', '\\' or ':', got \"${${_psp_knob}}\" "
                    "-- src/psp/save.cpp adds the separators, and a ':' on this console names a device")
            endif()
        endforeach()

        target_compile_definitions(pnes PRIVATE
                "PSP_SAVE_DIR=\"${PSP_SAVE_DIR}\""
                "PSP_SAVE_FILENAME=\"${PSP_SAVE_FILENAME}\"")

        find_program(FFMPEG ffmpeg REQUIRED)
        FetchContent_Declare(
                stb
                GIT_REPOSITORY https://github.com/nothings/stb.git
                GIT_TAG        ${STB_GIT_TAG}
        )
        FetchContent_MakeAvailable(stb)
        target_include_directories(pnes PRIVATE ${stb_SOURCE_DIR})

        set(PSP_ASSET_DIR ${CMAKE_BINARY_DIR}/psp-assets)
        set(PSP_OGG ${PSP_ASSET_DIR}/pc_audio.ogg)
        set(PSP_WAV ${CMAKE_SOURCE_DIR}/demo/audio/tracks/pc_audio.wav)
        file(MAKE_DIRECTORY ${PSP_ASSET_DIR})
        add_custom_command(
                OUTPUT ${PSP_OGG}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${PSP_ASSET_DIR}
                COMMAND ${FFMPEG} -y -loglevel error -i ${PSP_WAV}
                        -ar 44100 -ac 2 -c:a libvorbis -q:a 4 ${PSP_OGG}
                DEPENDS ${PSP_WAV}
                COMMENT "Encoding PSP audio: pc_audio.wav -> pc_audio.ogg (Vorbis, embedded)"
                VERBATIM)
        add_custom_target(psp-assets DEPENDS ${PSP_OGG})
        add_dependencies(pnes psp-assets)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/src/psp/audio.cpp
                PROPERTIES
                    OBJECT_DEPENDS  ${PSP_OGG}
                    COMPILE_OPTIONS "--embed-dir=${PSP_ASSET_DIR}")

        file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)
        list(FILTER DEMO_SOURCES EXCLUDE REGEX "/demo/gen/(title|gameOptions)/")
        file(GLOB DEMO_TITLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/title/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_TITLE_SOURCES})
        file(GLOB DEMO_GAMEOPTIONS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/gameOptions/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_GAMEOPTIONS_SOURCES})

        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/chr_rom.cpp
                PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/demo/chr/chr.bin")

        add_executable(demo ${DEMO_SOURCES})
        set_target_properties(demo PROPERTIES SUFFIX ".elf")
        target_link_libraries(demo PRIVATE pnes pspdisplay pspctrl pspaudio pspge psppower)

        target_compile_definitions(demo PRIVATE "GEN_TARGET_DIR=${DEMO_TITLE_DIR}")

        target_compile_options(demo PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

        include(CheckIPOSupported)
        check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
        if(_ipo_ok)
            foreach(_cfg Release MinSizeRel RelWithDebInfo)
                string(TOUPPER ${_cfg} _CFG)
                set_property(TARGET demo pnes
                        PROPERTY INTERPROCEDURAL_OPTIMIZATION_${_CFG} ON)
            endforeach()
        else()
            message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
        endif()

        create_pbp_file(
                TARGET      demo
                TITLE       "pnes demo"
                VERSION     "1.00"
        )

    elseif(TARGET_PLATFORM STREQUAL "ps2")
        project(pnes CXX)

        add_library(ps2_intsh_shim INTERFACE)
        target_include_directories(ps2_intsh_shim INTERFACE ${CMAKE_SOURCE_DIR}/src/ps2/intsh_shim)
        add_library(br0::intsh ALIAS ps2_intsh_shim)

        FetchContent_MakeAvailable(tuple)
        _make_consh_available()
        _fixup_fetched_cxx_std_targets()

        set(DEMO_TITLE_DIR psp)

        if(NOT DEFINED ENV{PS2SDK} OR NOT DEFINED ENV{GSKIT})
            message(FATAL_ERROR
                "PS2 build requires the PS2SDK and GSKIT env vars (set by the "
                "ps2dev/ps2dev Docker image -- see docker/Dockerfile.ps2).")
        endif()
        set(PS2SDK_ROOT "$ENV{PS2SDK}")
        set(GSKIT_ROOT  "$ENV{GSKIT}")

        file(GLOB PS2_SOURCES CONFIGURE_DEPENDS src/ps2/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)

        set(PS2_MUSIC "stream" CACHE STRING
                "PS2 music backend: stream (EE-decoded ADPCM), spu2 (SPU2 hardware voice), vorbis")
        set_property(CACHE PS2_MUSIC PROPERTY STRINGS stream spu2 vorbis)

        set(PS2_AUDIO_ALL
                ${CMAKE_SOURCE_DIR}/src/ps2/audio_stream.cpp
                ${CMAKE_SOURCE_DIR}/src/ps2/audio_spu2.cpp
                ${CMAKE_SOURCE_DIR}/src/ps2/audio.cpp)
        if(PS2_MUSIC STREQUAL "stream")
            set(PS2_AUDIO_SOURCE ${CMAKE_SOURCE_DIR}/src/ps2/audio_stream.cpp)
        elseif(PS2_MUSIC STREQUAL "spu2")
            set(PS2_AUDIO_SOURCE ${CMAKE_SOURCE_DIR}/src/ps2/audio_spu2.cpp)
        elseif(PS2_MUSIC STREQUAL "vorbis")
            set(PS2_AUDIO_SOURCE ${CMAKE_SOURCE_DIR}/src/ps2/audio.cpp)
        else()
            message(FATAL_ERROR "PS2_MUSIC must be stream, spu2 or vorbis, got '${PS2_MUSIC}'")
        endif()
        foreach(_unused IN LISTS PS2_AUDIO_ALL)
            if(NOT _unused STREQUAL PS2_AUDIO_SOURCE)
                list(REMOVE_ITEM PS2_SOURCES ${_unused})
            endif()
        endforeach()
        file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/all/*.cpp)
        add_library(pnes STATIC ${PS2_SOURCES} ${ALL_SOURCES})
        target_include_directories(pnes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
                ${PS2SDK_ROOT}/ee/include
                ${PS2SDK_ROOT}/common/include
                ${GSKIT_ROOT}/include
        )
        target_compile_definitions(pnes PUBLIC TARGET_PS2 _EE)
        if(DEFINED ALTERNATIVE_NAMETABLE)
            target_compile_definitions(pnes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
        endif()
        target_link_libraries(pnes PUBLIC br0::intsh br0::tuple br0::consh)

        find_program(FFMPEG ffmpeg REQUIRED)
        set(PS2_ASSET_DIR ${CMAKE_BINARY_DIR}/ps2-assets)
        set(PS2_WAV ${CMAKE_SOURCE_DIR}/demo/audio/tracks/pc_audio.wav)
        file(MAKE_DIRECTORY ${PS2_ASSET_DIR})

        if(PS2_MUSIC STREQUAL "stream")
            set(PS2_STREAM_RATE "44100" CACHE STRING
                    "Sample rate for the EE-decoded ADPCM music stream (44100 and 22050 are proven on hardware)")
            find_program(PS2_ADPENC adpenc HINTS ${PS2SDK_ROOT}/bin REQUIRED)

            set(PS2_ADP     ${PS2_ASSET_DIR}/pc_audio.adp)
            set(PS2_ADP_WAV ${PS2_ASSET_DIR}/pc_audio_mono.wav)
            add_custom_command(
                    OUTPUT ${PS2_ADP}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${PS2_ASSET_DIR}
                    COMMAND ${FFMPEG} -y -loglevel error -i ${PS2_WAV}
                            -ar ${PS2_STREAM_RATE} -ac 1 ${PS2_ADP_WAV}
                    COMMAND ${PS2_ADPENC} ${PS2_ADP_WAV} ${PS2_ADP}
                    DEPENDS ${PS2_WAV}
                    COMMENT "Encoding PS2 audio: pc_audio.wav -> pc_audio.adp (ADPCM stream @ ${PS2_STREAM_RATE}Hz mono, embedded)"
                    VERBATIM)
            add_custom_target(ps2-assets DEPENDS ${PS2_ADP})
            set(PS2_AUDIO_ASSET ${PS2_ADP})
            set_source_files_properties(${PS2_AUDIO_SOURCE}
                    PROPERTIES COMPILE_DEFINITIONS "PS2_STREAM_RATE=${PS2_STREAM_RATE}")
        elseif(PS2_MUSIC STREQUAL "spu2")
            set(PS2_ADPCM_RATE "11025" CACHE STRING
                    "Source sample rate for the SPU2 ADPCM music sample (mono; must fit SPU2's 2MB)")

            set(PS2_ADPCM_SECONDS "120" CACHE STRING
                    "Truncate the SPU2 ADPCM sample to N seconds (0 = whole track)")

            set(PS2_ADPCM_MAX_BYTES "786432" CACHE STRING
                    "Hard limit for the encoded SPU2 ADPCM sample, in bytes")
            if(PS2_ADPCM_SECONDS STREQUAL "0")
                set(PS2_ADPCM_TRUNCATE "")
            else()
                set(PS2_ADPCM_TRUNCATE -t ${PS2_ADPCM_SECONDS})
            endif()

            find_program(PS2_ADPENC adpenc HINTS ${PS2SDK_ROOT}/bin REQUIRED)

            set(PS2_ADP     ${PS2_ASSET_DIR}/pc_audio.adp)
            set(PS2_ADP_WAV ${PS2_ASSET_DIR}/pc_audio_mono.wav)
            add_custom_command(
                    OUTPUT ${PS2_ADP}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${PS2_ASSET_DIR}
                    COMMAND ${FFMPEG} -y -loglevel error -i ${PS2_WAV}
                            -ar ${PS2_ADPCM_RATE} -ac 1 ${PS2_ADPCM_TRUNCATE} ${PS2_ADP_WAV}
                    COMMAND ${PS2_ADPENC} -L ${PS2_ADP_WAV} ${PS2_ADP}
                    COMMAND ${CMAKE_COMMAND} -DADP=${PS2_ADP} -DMAX=${PS2_ADPCM_MAX_BYTES}
                            -P ${CMAKE_SOURCE_DIR}/cmake/ps2_check_adpcm_size.cmake
                    DEPENDS ${PS2_WAV} ${CMAKE_SOURCE_DIR}/cmake/ps2_check_adpcm_size.cmake
                    COMMENT "Encoding PS2 audio: pc_audio.wav -> pc_audio.adp (SPU2 ADPCM @ ${PS2_ADPCM_RATE}Hz mono, ${PS2_ADPCM_SECONDS}s=0:all, embedded)"
                    VERBATIM)
            add_custom_target(ps2-assets DEPENDS ${PS2_ADP})
            set(PS2_AUDIO_ASSET ${PS2_ADP})
        else()
            FetchContent_Declare(
                    stb
                    GIT_REPOSITORY https://github.com/nothings/stb.git
                    GIT_TAG        ${STB_GIT_TAG}
            )
            FetchContent_MakeAvailable(stb)
            target_include_directories(pnes PRIVATE ${stb_SOURCE_DIR})

            set(PS2_OGG ${PS2_ASSET_DIR}/pc_audio.ogg)
            add_custom_command(
                    OUTPUT ${PS2_OGG}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${PS2_ASSET_DIR}
                    COMMAND ${FFMPEG} -y -loglevel error -i ${PS2_WAV}
                            -ar 44100 -ac 2 -c:a libvorbis -q:a 4 ${PS2_OGG}
                    DEPENDS ${PS2_WAV}
                    COMMENT "Encoding PS2 audio: pc_audio.wav -> pc_audio.ogg (Vorbis, embedded)"
                    VERBATIM)
            add_custom_target(ps2-assets DEPENDS ${PS2_OGG})
            set(PS2_AUDIO_ASSET ${PS2_OGG})
        endif()
        add_dependencies(pnes ps2-assets)

        set_source_files_properties(${PS2_AUDIO_SOURCE}
                PROPERTIES
                    OBJECT_DEPENDS  ${PS2_AUDIO_ASSET}
                    COMPILE_OPTIONS "--embed-dir=${PS2_ASSET_DIR};--embed-dir=${PS2SDK_ROOT}/iop/irx")

        file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)
        list(FILTER DEMO_SOURCES EXCLUDE REGEX "/demo/gen/(title|gameOptions)/")
        file(GLOB DEMO_TITLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/title/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_TITLE_SOURCES})
        file(GLOB DEMO_GAMEOPTIONS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/gameOptions/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_GAMEOPTIONS_SOURCES})

        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/chr_rom.cpp
                PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/demo/chr/chr.bin")

        add_executable(demo ${DEMO_SOURCES})
        set_target_properties(demo PROPERTIES SUFFIX ".elf")
        target_link_directories(demo PRIVATE ${PS2SDK_ROOT}/ee/lib ${GSKIT_ROOT}/lib)
        target_link_libraries(demo PRIVATE pnes
                gskit gskit_toolkit dmakit
                pad audsrv patches
        )

        target_compile_definitions(demo PRIVATE "GEN_TARGET_DIR=${DEMO_TITLE_DIR}")

        target_compile_options(demo PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

        include(CheckIPOSupported)
        check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
        if(_ipo_ok)
            foreach(_cfg Release MinSizeRel RelWithDebInfo)
                string(TOUPPER ${_cfg} _CFG)
                set_property(TARGET demo pnes
                        PROPERTY INTERPROCEDURAL_OPTIMIZATION_${_CFG} ON)
            endforeach()
        else()
            message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
        endif()

    elseif(TARGET_PLATFORM STREQUAL "ps3")
        project(pnes CXX)

        add_library(ps3_intsh_shim INTERFACE)
        target_include_directories(ps3_intsh_shim INTERFACE ${CMAKE_SOURCE_DIR}/src/ps3/intsh_shim)
        add_library(br0::intsh ALIAS ps3_intsh_shim)

        FetchContent_MakeAvailable(tuple)
        _make_consh_available()
        _fixup_fetched_cxx_std_targets()

        set(DEMO_TITLE_DIR psp)

        if(NOT DEFINED ENV{PSL1GHT})
            message(FATAL_ERROR
                "PS3 build requires the PSL1GHT env var (set alongside PS3DEV by "
                "the pnes-ps3 Docker image -- see docker/Dockerfile.ps3).")
        endif()
        set(PS3DEV_ROOT  "$ENV{PS3DEV}")
        set(PSL1GHT_ROOT "$ENV{PSL1GHT}")

        file(GLOB PS3_SOURCES CONFIGURE_DEPENDS src/ps3/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
        file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/all/*.cpp)
        add_library(pnes STATIC ${PS3_SOURCES} ${ALL_SOURCES})
        target_include_directories(pnes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
                ${PSL1GHT_ROOT}/ppu/include
        )
        target_compile_definitions(pnes PUBLIC TARGET_PS3)
        if(DEFINED ALTERNATIVE_NAMETABLE)
            target_compile_definitions(pnes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
        endif()
        set(PS3_BOOT_TRACE 1 CACHE STRING "Trace PS3 init steps to the lv2 TTY (bring-up aid)")
        target_compile_definitions(pnes PUBLIC "PS3_BOOT_TRACE=${PS3_BOOT_TRACE}")

        target_link_libraries(pnes PUBLIC br0::intsh br0::tuple br0::consh)

        target_compile_options(pnes PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

        find_program(FFMPEG ffmpeg REQUIRED)
        FetchContent_Declare(
                stb
                GIT_REPOSITORY https://github.com/nothings/stb.git
                GIT_TAG        ${STB_GIT_TAG}
        )
        FetchContent_MakeAvailable(stb)
        target_include_directories(pnes PRIVATE ${stb_SOURCE_DIR})

        set(PS3_ASSET_DIR ${CMAKE_BINARY_DIR}/ps3-assets)
        set(PS3_OGG ${PS3_ASSET_DIR}/pc_audio.ogg)
        set(PS3_WAV ${CMAKE_SOURCE_DIR}/demo/audio/tracks/pc_audio.wav)
        file(MAKE_DIRECTORY ${PS3_ASSET_DIR})
        add_custom_command(
                OUTPUT ${PS3_OGG}
                COMMAND ${CMAKE_COMMAND} -E make_directory ${PS3_ASSET_DIR}
                COMMAND ${FFMPEG} -y -loglevel error -i ${PS3_WAV}
                        -ar 48000 -ac 2 -c:a libvorbis -q:a 4 ${PS3_OGG}
                DEPENDS ${PS3_WAV}
                COMMENT "Encoding PS3 audio: pc_audio.wav -> pc_audio.ogg (Vorbis 48kHz, embedded)"
                VERBATIM)
        add_custom_target(ps3-assets DEPENDS ${PS3_OGG})
        add_dependencies(pnes ps3-assets)

        set_source_files_properties(${CMAKE_SOURCE_DIR}/src/ps3/audio.cpp
                PROPERTIES
                    OBJECT_DEPENDS  ${PS3_OGG}
                    COMPILE_OPTIONS "--embed-dir=${PS3_ASSET_DIR}")

        file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)
        list(FILTER DEMO_SOURCES EXCLUDE REGEX "/demo/gen/(title|gameOptions)/")
        file(GLOB DEMO_TITLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/title/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_TITLE_SOURCES})
        file(GLOB DEMO_GAMEOPTIONS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/gameOptions/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_GAMEOPTIONS_SOURCES})

        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/chr_rom.cpp
                PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/demo/chr/chr.bin")

        add_executable(demo ${DEMO_SOURCES})
        set_target_properties(demo PROPERTIES SUFFIX ".elf")
        target_link_directories(demo PRIVATE ${PSL1GHT_ROOT}/ppu/lib)
        target_link_libraries(demo PRIVATE pnes
                rsx gcm_sys
                io
                sysutil sysmodule
                audio
                lv2 m
        )

        target_compile_definitions(demo PRIVATE "GEN_TARGET_DIR=${DEMO_TITLE_DIR}")

        target_compile_options(demo PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

        # Pack the linked ELF into the SELF image the console boots, all three
        # ps3dev host tools:
        #
        #   sprxlinker  finishes the PRX import/export relocations the linker
        #               leaves open. Not optional -- PSL1GHT's own rules run it
        #               straight after linking -- and doing it in place leaves
        #               demo.elf itself bootable, which is how RPCS3 can
        #               exercise this backend with the SELF layer removed.
        #   fself       wraps it as EBOOT.BIN, which is what the XMB expects.
        #   make_self   wraps it again as EBOOT.CEX.BIN.
        #
        # The difference is the SCE header's key_revision: fself writes 0x8000,
        # the agreed "unsigned, read the segments raw" marker loaders check
        # first, where make_self compresses them.
        find_program(PS3_SPRXLINKER sprxlinker HINTS ${PS3DEV_ROOT}/bin REQUIRED)
        find_program(PS3_FSELF      fself      HINTS ${PS3DEV_ROOT}/bin REQUIRED)
        find_program(PS3_MAKE_SELF  make_self  HINTS ${PS3DEV_ROOT}/bin REQUIRED)
        add_custom_command(TARGET demo POST_BUILD
                COMMAND ${PS3_SPRXLINKER} $<TARGET_FILE:demo>
                COMMAND ${PS3_FSELF} $<TARGET_FILE:demo> ${CMAKE_BINARY_DIR}/EBOOT.BIN
                COMMAND ${PS3_MAKE_SELF} $<TARGET_FILE:demo> ${CMAKE_BINARY_DIR}/EBOOT.CEX.BIN
                COMMENT "Packing PS3 SELF: demo.elf -> EBOOT.BIN (sprxlinker + fself) + EBOOT.CEX.BIN (make_self)"
                VERBATIM)


    elseif(TARGET_PLATFORM STREQUAL "nds" OR TARGET_PLATFORM STREQUAL "dsi")
        project(pnes CXX)

        FetchContent_MakeAvailable(intsh tuple)
        _make_consh_available()
        _fixup_fetched_cxx_std_targets()

        file(GLOB NDS_SOURCES CONFIGURE_DEPENDS src/nds/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
        file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/all/*.cpp)
        add_library(pnes STATIC ${NDS_SOURCES} ${ALL_SOURCES})
        target_include_directories(pnes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
        )
        target_compile_definitions(pnes PUBLIC TARGET_NDS)
        if(TARGET_PLATFORM STREQUAL "dsi")
            target_compile_definitions(pnes PUBLIC TARGET_DSI)
            set(DEMO_TITLE_DIR dsi)
        else()
            set(DEMO_TITLE_DIR ds)
        endif()
        if(DEFINED ALTERNATIVE_NAMETABLE)
            target_compile_definitions(pnes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
        endif()
        target_link_libraries(pnes PUBLIC br0::intsh br0::tuple br0::consh)

        file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)
        list(FILTER DEMO_SOURCES EXCLUDE REGEX "/demo/gen/(title|gameOptions)/")
        file(GLOB DEMO_TITLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/title/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_TITLE_SOURCES})
        file(GLOB DEMO_GAMEOPTIONS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/gameOptions/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_GAMEOPTIONS_SOURCES})

        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/chr_rom.cpp
                PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/demo/chr/chr.bin")

        add_executable(demo_elf ${DEMO_SOURCES})
        set_target_properties(demo_elf PROPERTIES OUTPUT_NAME demo SUFFIX ".elf")
        target_link_libraries(demo_elf PRIVATE pnes nds9)

        target_compile_definitions(demo_elf PRIVATE "GEN_TARGET_DIR=${DEMO_TITLE_DIR}")

        target_compile_options(demo_elf PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

        include(CheckIPOSupported)
        check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
        if(_ipo_ok)
            foreach(_cfg Release MinSizeRel RelWithDebInfo)
                string(TOUPPER ${_cfg} _CFG)
                set_property(TARGET demo_elf pnes
                        PROPERTY INTERPROCEDURAL_OPTIMIZATION_${_CFG} ON)
            endforeach()
        else()
            message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
        endif()

        nds_create_rom(demo
                ARM9 demo_elf
                NAME "pnes demo"
                SUBTITLE1 "NES engine demo on DS"
                SUBTITLE2 "github.com/brette"
                OUTPUT ${CMAKE_BINARY_DIR}/demo.nds
        )

    elseif(TARGET_PLATFORM STREQUAL "gba")
        project(pnes CXX)

        FetchContent_MakeAvailable(intsh tuple)
        _make_consh_available()
        _fixup_fetched_cxx_std_targets()

        set(DEMO_TITLE_DIR gba)

        file(GLOB GBA_SOURCES CONFIGURE_DEPENDS src/gba/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
        file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/all/*.cpp)
        add_library(pnes STATIC ${GBA_SOURCES} ${ALL_SOURCES})
        target_include_directories(pnes PUBLIC
                ${CMAKE_SOURCE_DIR}/include
                ${CMAKE_SOURCE_DIR}/include/umbrella
        )
        target_compile_definitions(pnes PUBLIC TARGET_GBA)
        if(DEFINED ALTERNATIVE_NAMETABLE)
            target_compile_definitions(pnes PUBLIC "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
        endif()
        target_link_libraries(pnes PUBLIC br0::intsh br0::tuple br0::consh)

        file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)
        list(FILTER DEMO_SOURCES EXCLUDE REGEX "/demo/gen/(title|gameOptions)/")
        file(GLOB DEMO_TITLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/title/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_TITLE_SOURCES})
        file(GLOB DEMO_GAMEOPTIONS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/gameOptions/${DEMO_TITLE_DIR}/*.cpp")
        list(APPEND DEMO_SOURCES ${DEMO_GAMEOPTIONS_SOURCES})

        set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/chr_rom.cpp
                PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/demo/chr/chr.bin")

        add_executable(demo ${DEMO_SOURCES})
        set_target_properties(demo PROPERTIES SUFFIX ".elf")
        target_link_libraries(demo PRIVATE pnes)

        target_compile_definitions(demo PRIVATE "GEN_TARGET_DIR=${DEMO_TITLE_DIR}")

        target_compile_options(demo PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

        include(CheckIPOSupported)
        check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
        if(_ipo_ok)
            foreach(_cfg Release MinSizeRel RelWithDebInfo)
                string(TOUPPER ${_cfg} _CFG)
                set_property(TARGET demo pnes
                        PROPERTY INTERPROCEDURAL_OPTIMIZATION_${_CFG} ON)
            endforeach()
        else()
            message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
        endif()

        gba_create_rom(demo
                OUTPUT ${CMAKE_BINARY_DIR}/demo.gba
                TITLE "PNES"
                GAMECODE "CPNE"
                MAKERCODE "00"
        )

else()
    project(pnes CXX)

    FetchContent_MakeAvailable(intsh tuple)
    _make_consh_available()
    _fixup_fetched_cxx_std_targets()

    set(DEMO_TITLE_DIR pc)

    if(TARGET_PLATFORM STREQUAL "mac")
        set(PLATFORM_DEFINE TARGET_MACOS)
    elseif(TARGET_PLATFORM STREQUAL "win")
        set(PLATFORM_DEFINE TARGET_WINDOWS)
    elseif(TARGET_PLATFORM STREQUAL "linux")
        set(PLATFORM_DEFINE TARGET_LINUX)
    else()
        message(FATAL_ERROR "Set -DTARGET_PLATFORM to one of: nes, win, mac, linux")
    endif()

    if(TARGET_PLATFORM STREQUAL "mac" AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64"
            AND NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(arm64|ARM64)$")
        message(FATAL_ERROR
                "This preset targets arm64 (Apple Silicon), but the host CPU "
                "is '${CMAKE_HOST_SYSTEM_PROCESSOR}'. mac-arm64 builds ON "
                "Apple Silicon hardware, not cross-compile to it from an "
                "Intel Mac -- run this natively on an Apple Silicon machine "
                "instead.")
    endif()

    if(DEFINED ALTERNATIVE_NAMETABLE)
        list(APPEND PLATFORM_DEFINE "ALTERNATIVE_NAMETABLE=${ALTERNATIVE_NAMETABLE}")
    endif()

    include(FetchContent)

    if(TARGET_PLATFORM STREQUAL "linux")
        if(CMAKE_CXX_COMPILER_TARGET MATCHES "^i[3-6]86-")
            foreach(_x86_pkgconfig_dir
                    /usr/lib32/pkgconfig
                    /usr/lib/i386-linux-gnu/pkgconfig)
                if(EXISTS ${_x86_pkgconfig_dir})
                    set(ENV{PKG_CONFIG_LIBDIR} "${_x86_pkgconfig_dir}:/usr/share/pkgconfig")
                    break()
                endif()
            endforeach()
            if(NOT DEFINED ENV{PKG_CONFIG_LIBDIR})
                message(FATAL_ERROR
                        "32-bit Linux build: couldn't find a 32-bit pkgconfig "
                        "dir (checked /usr/lib32/pkgconfig, "
                        "/usr/lib/i386-linux-gnu/pkgconfig). Install the "
                        "32-bit sdl3/libusb-1.0 dev packages for your distro, "
                        "or set PKG_CONFIG_LIBDIR yourself before configuring.")
            endif()
        endif()

        if(CMAKE_CXX_COMPILER_TARGET MATCHES "^aarch64-"
                AND NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
            if(EXISTS /usr/lib/aarch64-linux-gnu/pkgconfig)
                set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig")
            else()
                message(FATAL_ERROR
                        "Cross-compiling to aarch64 Linux: couldn't find "
                        "/usr/lib/aarch64-linux-gnu/pkgconfig. Install the "
                        "aarch64 sdl3/libusb-1.0 dev packages for your distro, "
                        "or set PKG_CONFIG_LIBDIR yourself before configuring.")
            endif()
        endif()

        find_package(PkgConfig REQUIRED)
        pkg_check_modules(SDL3   REQUIRED IMPORTED_TARGET sdl3)
        pkg_check_modules(LibUSB REQUIRED IMPORTED_TARGET libusb-1.0)

        if(NOT TARGET SDL3::SDL3-static)
            add_library(SDL3::SDL3-static ALIAS PkgConfig::SDL3)
        endif()
        if(NOT TARGET LibUSB::LibUSB)
            add_library(LibUSB::LibUSB ALIAS PkgConfig::LibUSB)
        endif()
    else()
        set(SDL3_GIT_TAG "release-3.2.16" CACHE STRING "SDL3 git tag to fetch")

        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON  CACHE BOOL "" FORCE)
        set(SDL_TEST   OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS  OFF CACHE BOOL "" FORCE)
        set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
        set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
        set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
        set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
        set(SDL_POWER OFF CACHE BOOL "" FORCE)
        set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
        set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
        set(SDL_GPU OFF CACHE BOOL "" FORCE)
        set(SDL_RENDER_GPU OFF CACHE BOOL "" FORCE)

        FetchContent_Declare(
                SDL3
                GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                GIT_TAG        ${SDL3_GIT_TAG}
                GIT_SHALLOW    TRUE
        )
        FetchContent_MakeAvailable(SDL3)
    endif()

    file(GLOB SDL3_SOURCES CONFIGURE_DEPENDS src/SDL3/*.cpp src/emu/*.cpp src/emu/mappers/*.cpp)
    file(GLOB_RECURSE ALL_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/all/*.cpp)
    add_library(pnes STATIC ${SDL3_SOURCES} ${ALL_SOURCES})
    target_include_directories(pnes PUBLIC
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_SOURCE_DIR}/include/umbrella
    )
    target_compile_definitions(pnes PUBLIC ${PLATFORM_DEFINE})
    target_link_libraries(pnes PUBLIC SDL3::SDL3-static)
    target_link_libraries(pnes PUBLIC br0::intsh br0::tuple br0::consh)

    FetchContent_Declare(
            stb
            GIT_REPOSITORY https://github.com/nothings/stb.git
            GIT_TAG        ${STB_GIT_TAG}
    )
    FetchContent_MakeAvailable(stb)
    target_include_directories(pnes PRIVATE ${stb_SOURCE_DIR})

    if(TARGET_PLATFORM STREQUAL "linux")
        target_link_libraries(pnes PUBLIC PkgConfig::LibUSB)
    endif()

    file(GLOB_RECURSE DEMO_SOURCES CONFIGURE_DEPENDS demo/*.cpp)
    list(FILTER DEMO_SOURCES EXCLUDE REGEX "/demo/gen/(title|gameOptions)/")
    file(GLOB DEMO_TITLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/title/${DEMO_TITLE_DIR}/*.cpp")
    list(APPEND DEMO_SOURCES ${DEMO_TITLE_SOURCES})
    file(GLOB DEMO_GAMEOPTIONS_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/demo/gen/gameOptions/${DEMO_TITLE_DIR}/*.cpp")
    list(APPEND DEMO_SOURCES ${DEMO_GAMEOPTIONS_SOURCES})

    set_source_files_properties(${CMAKE_SOURCE_DIR}/demo/src/graphics/chr_rom.cpp
            PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/demo/chr/chr.bin")

    add_executable(demo ${DEMO_SOURCES})
    target_link_libraries(demo PRIVATE pnes)

    target_compile_definitions(demo PRIVATE "GEN_TARGET_DIR=${DEMO_TITLE_DIR}")

    target_compile_options(demo PRIVATE $<$<CXX_COMPILER_ID:Clang>:-Wno-c23-extensions>)

    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_err)
    if(_ipo_ok)
        foreach(_cfg Release MinSizeRel RelWithDebInfo)
            string(TOUPPER ${_cfg} _CFG)
            set_property(TARGET demo pnes
                    PROPERTY INTERPROCEDURAL_OPTIMIZATION_${_CFG} ON)
        endforeach()
    else()
        message(STATUS "IPO/LTO not supported by this toolchain: ${_ipo_err}")
    endif()

    add_custom_command(TARGET demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/demo/audio/tracks
            $<TARGET_FILE_DIR:demo>/tracks
    )

    add_custom_command(TARGET demo POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/demo/audio/sfx
            $<TARGET_FILE_DIR:demo>/sfx
    )

    set(_PNES_STRIP "${CMAKE_STRIP}")
    if(CMAKE_CXX_COMPILER_TARGET MATCHES "^aarch64-"
            AND NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        find_program(_PNES_AARCH64_STRIP aarch64-linux-gnu-strip)
        if(_PNES_AARCH64_STRIP)
            set(_PNES_STRIP "${_PNES_AARCH64_STRIP}")
        endif()
    endif()

    if(_PNES_STRIP AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(TARGET demo POST_BUILD
                COMMAND ${_PNES_STRIP} $<TARGET_FILE:demo>
                COMMENT "Stripping demo executable"
        )
    endif()

    if(TARGET_PLATFORM STREQUAL "linux")
        find_program(LINUXDEPLOY linuxdeploy)
        if(LINUXDEPLOY)
            set(APPDIR "${CMAKE_BINARY_DIR}/AppDir")

            if(CMAKE_CXX_COMPILER_TARGET MATCHES "^i[3-6]86-")
                set(_LINUX_MULTIARCH_TRIPLET "i386-linux-gnu")
            elseif(CMAKE_CXX_COMPILER_TARGET MATCHES "^aarch64-")
                set(_LINUX_MULTIARCH_TRIPLET "aarch64-linux-gnu")
            else()
                set(_LINUX_MULTIARCH_TRIPLET "x86_64-linux-gnu")
            endif()

            set(_PNES_LINUXDEPLOY_ENV "")
            if(CMAKE_CXX_COMPILER_TARGET MATCHES "^aarch64-"
                    AND NOT CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
                set(_PNES_LINUXDEPLOY_ENV "NO_STRIP=1")
            endif()

            configure_file(
                    "${CMAKE_SOURCE_DIR}/docker/demo.desktop.in"
                    "${CMAKE_BINARY_DIR}/demo.desktop"
                    @ONLY
            )

            file(WRITE "${CMAKE_BINARY_DIR}/AppRun"
"#!/bin/bash
HERE=\"$(dirname \"$(readlink -f \"\${0}\")\")\"
export LD_LIBRARY_PATH=\"\${HERE}/usr/lib:\${LD_LIBRARY_PATH}\"
cd \"\${HERE}/usr/bin\"
exec ./demo \"\$@\"
")

            set(_APPDIR_LICENSES "${APPDIR}/usr/share/doc/pnes")

            add_custom_target(demo-appimage
                    COMMAND ${CMAKE_COMMAND} -E rm -rf "${APPDIR}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${APPDIR}/usr/bin"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${_APPDIR_LICENSES}"
                    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:demo> "${APPDIR}/usr/bin/demo"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${CMAKE_SOURCE_DIR}/demo/audio/tracks" "${APPDIR}/usr/bin/tracks"
                    COMMAND ${CMAKE_COMMAND} -E copy_directory
                            "${CMAKE_SOURCE_DIR}/demo/audio/sfx"    "${APPDIR}/usr/bin/sfx"
                    COMMAND ${CMAKE_COMMAND} -E copy
                            "${CMAKE_SOURCE_DIR}/LICENSE"     "${_APPDIR_LICENSES}/LICENSE"
                    COMMAND ${CMAKE_COMMAND} -E copy
                            "${CMAKE_BINARY_DIR}/AppRun"      "${APPDIR}/AppRun"
                    COMMAND chmod +x "${APPDIR}/AppRun"
                    COMMAND ${CMAKE_COMMAND} -E env ${_PNES_LINUXDEPLOY_ENV}
                            ${LINUXDEPLOY}
                            --appdir "${APPDIR}"
                            --executable "${APPDIR}/usr/bin/demo"
                            --desktop-file "${CMAKE_BINARY_DIR}/demo.desktop"
                            --icon-file "${CMAKE_SOURCE_DIR}/demo/demo.png"
                            --library /usr/lib/${_LINUX_MULTIARCH_TRIPLET}/libasound.so.2
                            --output appimage
                    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
                    DEPENDS demo
                    COMMENT "Packaging AppImage"
                    VERBATIM
            )
        else()
            message(STATUS
                    "linuxdeploy not found on PATH; 'demo-appimage' target disabled. "
                    "Build inside the provided Docker image, or install linuxdeploy locally.")
        endif()
    endif()
endif()

if(NOT REGION STREQUAL "US" AND NOT REGION STREQUAL "EU" AND NOT REGION STREQUAL "JP")
    message(FATAL_ERROR
        "REGION must be US, EU or JP, got: \"${REGION}\"")
endif()
target_compile_definitions(pnes PUBLIC "REGION=\"${REGION}\"")

target_compile_definitions(pnes PUBLIC "REGION_${REGION}")

if(SAVE_FILE)
    target_compile_definitions(pnes PUBLIC "SAVE_FILE=${SAVE_FILE}")
endif()

if(TARGET demo_elf)
    set(DEMO_EXE_TARGET demo_elf)
else()
    set(DEMO_EXE_TARGET demo)
endif()
target_compile_options(${DEMO_EXE_TARGET} PRIVATE -include ${CMAKE_SOURCE_DIR}/demo/src/graphics/charmaps.hpp)