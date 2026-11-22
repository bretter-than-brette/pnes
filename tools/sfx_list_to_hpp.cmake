# Turns FamiStudio generated sound-effect name list into a C++ enum, so call
# sites say sfx::select rather than 1. Run at build time with cmake -P.

if(NOT SFX_LIST_INC OR NOT OUT_HPP)
    message(FATAL_ERROR
            "sfx_list_to_hpp.cmake: -DSFX_LIST_INC and -DOUT_HPP are both required.")
endif()

if(NOT NAMESPACE)
    set(NAMESPACE "sfx")
endif()

if(NOT EXISTS "${SFX_LIST_INC}")
    message(FATAL_ERROR
            "sfx_list_to_hpp.cmake: ${SFX_LIST_INC} does not exist. It is written by "
            "FamiStudio's SFX export when -famistudio-asm-sfx-generate-list is passed; "
            "if the export ran without that flag, no list was produced.")
endif()

file(STRINGS "${SFX_LIST_INC}" _lines)

set(_entries "")
set(_count "")
foreach(_line IN LISTS _lines)
    if(NOT _line MATCHES "^[ \t]*([A-Za-z_][A-Za-z0-9_]*)[ \t]*=[ \t]*([0-9]+)[ \t]*$")
        continue()
    endif()
    set(_name "${CMAKE_MATCH_1}")
    set(_value "${CMAKE_MATCH_2}")

    if(_name STREQUAL "sfx_max")
        set(_count "${_value}")
        continue()
    endif()

    string(REGEX REPLACE "^sfx_" "" _enumerator "${_name}")

    if(NOT _enumerator MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR
                "sfx_list_to_hpp.cmake: effect name \"${_name}\" does not yield a valid "
                "C++ identifier (\"${_enumerator}\"). Rename the song in the FamiStudio "
                "SFX project.")
    endif()

    string(APPEND _entries "    ${_enumerator} = ${_value},\n")
endforeach()

if(_count STREQUAL "")
    message(FATAL_ERROR
            "sfx_list_to_hpp.cmake: no sfx_max in ${SFX_LIST_INC}. The exporter writes "
            "it after the effect list, so either the file is truncated or its format "
            "has changed.")
endif()

if(_entries STREQUAL "")
    set(_enum_block "// No effects in the project -- nothing to enumerate.\n")
else()
    set(_enum_block "enum Effect : unsigned char {\n${_entries}};\n")
endif()

get_filename_component(_inc_name "${SFX_LIST_INC}" NAME)

file(CONFIGURE OUTPUT "${OUT_HPP}" @ONLY CONTENT
"// GENERATED from @_inc_name@ by tools/sfx_list_to_hpp.cmake -- do not edit.
//
// @_inc_name@ is itself generated, by FamiStudio's SFX export, from the
// project's own song names and in the same run that produced the effect data.
// Both are build artifacts: change the FamiStudio project, not this file.
//
// Indices are POSITIONAL -- effect N is the Nth song in the SFX project -- so
// these names exist precisely so no call site has to hardcode one.

namespace @NAMESPACE@ {

@_enum_block@
// Number of effects exported. The valid range is 0 .. count - 1, and NOTHING
// bounds-checks it: audio::SfxPlay hands the index straight to the engine (see
// include/pnes/audio.hpp). Passing an enumerator from above is what makes that
// safe. Not to be confused with audio::sfxChannels, which is how many effects
// can sound AT ONCE.
constexpr unsigned char count = @_count@;

} // namespace @NAMESPACE@
")

message(STATUS "sfx_list_to_hpp: ${_inc_name} -> ${OUT_HPP} (${_count} effects)")
