# wiiu_pack.cmake -- build-time packer for the optional installable Wii U title.
#
# Invoked by the `demo-wup` target with the unpacked tree already assembled under
# WUP_DIR; this runs NUSPacker over it.

set(_key "${COMMON_KEY}")
if(NOT _key)
    set(_key "$ENV{WIIU_COMMON_KEY}")
endif()
if(NOT NUSPACKER_JAR)
    set(NUSPACKER_JAR "$ENV{WIIU_NUSPACKER_JAR}")
endif()
if(_key AND EXISTS "${_key}")
    file(READ "${_key}" _key)
endif()
string(REGEX MATCH "[0-9A-Fa-f]+" _key "${_key}")

if(NOT _key OR NOT NUSPACKER_JAR OR NOT EXISTS "${NUSPACKER_JAR}")
    message(STATUS "")
    message(STATUS "demo-wup: unpacked title tree is ready at:")
    message(STATUS "    ${WUP_DIR}   (code/ content/ meta/)")
    if(NOT _key)
        message(STATUS "demo-wup: WIIU_COMMON_KEY not set -- skipping WUP encryption.")
        message(STATUS "          Pack it yourself with NUSPacker, or set WIIU_COMMON_KEY")
        message(STATUS "          (32-hex string or file path) and rebuild demo-wup.")
    endif()
    if(NOT NUSPACKER_JAR OR NOT EXISTS "${NUSPACKER_JAR}")
        message(STATUS "demo-wup: NUSPacker.jar not found -- set WIIU_NUSPACKER_JAR.")
    endif()
    message(STATUS "")
    return()
endif()

string(LENGTH "${_key}" _keylen)
if(NOT _keylen EQUAL 32)
    message(FATAL_ERROR "demo-wup: WIIU_COMMON_KEY must be 32 hex characters (got ${_keylen})")
endif()

find_program(_java NAMES java)
if(NOT _java)
    message(FATAL_ERROR "demo-wup: java not found on PATH (needed to run NUSPacker)")
endif()

file(MAKE_DIRECTORY "${WUP_OUT}")

message(STATUS "demo-wup: packing installable WUP -> ${WUP_OUT}")
execute_process(
        COMMAND "${_java}" -jar "${NUSPACKER_JAR}"
                -in  "${WUP_DIR}"
                -out "${WUP_OUT}"
                -encryptKeyWith "${_key}"
        RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "demo-wup: NUSPacker failed (exit ${_rc})")
endif()
message(STATUS "demo-wup: WUP written to ${WUP_OUT}")
