#!/usr/bin/env bash
#
# profsym.sh -- resolves OGC PC-sampler hex addresses to function and file:line
# against the linked ELF symbols. Read the addresses off the on-screen overlay.
#
set -euo pipefail

ELF="cmake-build-ci-gc-release/demo.elf"
ADDR2LINE="${DEVKITPRO:-/opt/devkitpro}/devkitPPC/bin/powerpc-eabi-addr2line"

args=()
for a in "$@"; do
    if [[ "$a" == *.elf ]]; then ELF="$a"; else args+=("0x${a#0x}"); fi
done

[[ -x "$ADDR2LINE" ]] || { echo "addr2line not found: $ADDR2LINE" >&2; exit 1; }
[[ -f "$ELF" ]]       || { echo "ELF not found: $ELF (build it, or pass the path)" >&2; exit 1; }
[[ ${#args[@]} -gt 0 ]] || { echo "usage: $0 [demo.elf] <hexaddr> [hexaddr ...]" >&2; exit 1; }

"$ADDR2LINE" -e "$ELF" -f -C -i "${args[@]}"
