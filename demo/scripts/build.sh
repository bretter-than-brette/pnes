#!/usr/bin/env bash
#
# Workhorse build script: configures via a CMake preset and builds one target.
#
#   demo/scripts/build.sh <target> [release|debug] [options]
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

usage() { sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }

TARGETS="nes linux linux-x86 linux-arm64 linux-docker \
win-x64 win-x86 win-arm64 mac-arm64 mac-x64 \
ctr gc wii switch wiiu nds ps2"

PLATFORM=""
BUILD_TYPE="release"
CLEAN=0
CMAKE_TARGET=""
JOBS=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        release|debug)       BUILD_TYPE="$1" ;;
        -c|--clean)          CLEAN=1 ;;
        -t|--target)         CMAKE_TARGET="$2"; shift ;;
        -j|--jobs)           JOBS="$2"; shift ;;
        -h|--help)           usage; exit 0 ;;
        win)                 PLATFORM="win-x64" ;;
        mac)                 PLATFORM="mac-arm64" ;;
        *)
            if [[ " ${TARGETS} " == *" $1 "* ]]; then
                PLATFORM="$1"
            else
                echo "error: unknown argument '$1'" >&2; usage >&2; exit 2
            fi
            ;;
    esac
    shift
done

if [[ -z "$PLATFORM" ]]; then
    echo "error: no target given (one of: ${TARGETS})" >&2
    usage >&2
    exit 2
fi

PRESET="${PLATFORM}-${BUILD_TYPE}"
BUILD_DIR="${ROOT}/builds/cmake-build-${PRESET}"

if [[ "$CLEAN" -eq 1 && -d "$BUILD_DIR" ]]; then
    echo ">> cleaning ${BUILD_DIR}"
    rm -rf "$BUILD_DIR"
fi

echo ">> configuring preset '${PRESET}'"
cmake --preset "$PRESET" -S "$ROOT"

build_args=(--build "$BUILD_DIR")
[[ -n "$CMAKE_TARGET" ]] && build_args+=(--target "$CMAKE_TARGET")
if [[ -n "$JOBS" ]]; then
    build_args+=(--parallel "$JOBS")
else
    build_args+=(--parallel)
fi

echo ">> building preset '${PRESET}'${CMAKE_TARGET:+ (target: ${CMAKE_TARGET})}"
cmake "${build_args[@]}"

echo ">> done -> ${BUILD_DIR}"
